param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [int]$Port = 18080
)

$ErrorActionPreference = "Stop"
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ServerExe = Join-Path $BuildDir "socket_server.exe"
$ClientExe = Join-Path $BuildDir "socket_client.exe"
$DocumentRoot = Join-Path $BuildDir "www"
$ServerLog = Join-Path $BuildDir "server.log"
$script:Passed = 0
$script:ServerProcess = $null

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    $script:Passed++
    Write-Host "[PASS] $Message" -ForegroundColor Green
}

function Start-TestServer([string]$Suffix) {
    $stdout = Join-Path $BuildDir "smoke-server-$Suffix.out.log"
    $stderr = Join-Path $BuildDir "smoke-server-$Suffix.err.log"
    Remove-Item $stdout, $stderr -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $ServerExe `
        -ArgumentList @($Port, "www", "127.0.0.1") `
        -WorkingDirectory $BuildDir `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru
    Start-Sleep -Milliseconds 500
    if ($process.HasExited) {
        $errorText = Get-Content $stderr -Raw -ErrorAction SilentlyContinue
        throw "Server exited during startup. $errorText"
    }
    return $process
}

function Stop-TestServer {
    if ($script:ServerProcess -and -not $script:ServerProcess.HasExited) {
        Stop-Process -Id $script:ServerProcess.Id -Force
        $script:ServerProcess.WaitForExit()
    }
    $script:ServerProcess = $null
}

function Invoke-DemoClient([string]$Path) {
    $output = & $ClientExe "127.0.0.1" $Port $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Client failed for ${Path}: $output"
    }
    return ($output -join "`n")
}

function Send-RawRequest([string]$Request) {
    $tcp = [System.Net.Sockets.TcpClient]::new()
    try {
        $tcp.ReceiveTimeout = 5000
        $tcp.SendTimeout = 5000
        $tcp.Connect("127.0.0.1", $Port)
        $stream = $tcp.GetStream()
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($Request)
        $stream.Write($bytes, 0, $bytes.Length)
        $tcp.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)
        $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
        return $reader.ReadToEnd()
    }
    finally {
        $tcp.Dispose()
    }
}

function Test-ParallelClients {
    $clients = @()
    $streams = @()
    try {
        1..10 | ForEach-Object {
            $tcp = [System.Net.Sockets.TcpClient]::new()
            $tcp.ReceiveTimeout = 5000
            $tcp.SendTimeout = 5000
            $tcp.Connect("127.0.0.1", $Port)
            $clients += $tcp
            $streams += $tcp.GetStream()
        }

        $maxActive = 0
        for ($attempt = 0; $attempt -lt 30 -and $maxActive -lt 10; $attempt++) {
            Start-Sleep -Milliseconds 50
            if (Test-Path $ServerLog) {
                $matches = Select-String -Path $ServerLog -Pattern "active=(\d+)" -AllMatches
                $values = @($matches.Matches | ForEach-Object { [int]$_.Groups[1].Value })
                if ($values.Count -gt 0) {
                    $maxActive = ($values | Measure-Object -Maximum).Maximum
                }
            }
        }
        Assert-True ($maxActive -ge 10) "Server keeps 10 client connections active concurrently"

        $request = [System.Text.Encoding]::ASCII.GetBytes(
            "GET /hello.txt HTTP/1.1`r`nHost: 127.0.0.1`r`nConnection: close`r`n`r`n")
        foreach ($stream in $streams) {
            $stream.Write($request, 0, $request.Length)
        }
        foreach ($tcp in $clients) {
            $tcp.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)
        }

        foreach ($stream in $streams) {
            $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
            $response = $reader.ReadToEnd()
            Assert-True ($response.StartsWith("HTTP/1.1 200 OK")) "Parallel client receives HTTP 200"
            Assert-True ($response.Contains("Hello from SocketDemo!")) "Parallel client receives correct body"
        }
    }
    finally {
        foreach ($tcp in $clients) {
            $tcp.Dispose()
        }
    }
}

if (-not (Test-Path $ServerExe) -or -not (Test-Path $ClientExe)) {
    throw "Build outputs are missing. Run cmake --build build first."
}

Remove-Item $ServerLog -ErrorAction SilentlyContinue

try {
    $script:ServerProcess = Start-TestServer "first"

    $index = Invoke-DemoClient "/index.html"
    Assert-True ($index.Contains("HTTP/1.1 200 OK")) "GET /index.html returns 200"
    Assert-True ($index.Contains("The socket is live.")) "GET /index.html returns the website"

    $hello = Invoke-DemoClient "/hello.txt"
    Assert-True ($hello.Contains("HTTP/1.1 200 OK")) "GET /hello.txt returns 200"
    Assert-True ($hello.Contains("Hello from SocketDemo!")) "GET /hello.txt returns the correct body"

    $missing = Invoke-DemoClient "/missing.txt"
    Assert-True ($missing.Contains("HTTP/1.1 404 Not Found")) "Missing file returns 404"

    $malformed = Send-RawRequest "BROKEN`r`n`r`n"
    Assert-True ($malformed.StartsWith("HTTP/1.1 400 Bad Request")) "Malformed request returns 400"

    $post = Send-RawRequest "POST /index.html HTTP/1.1`r`nHost: 127.0.0.1`r`nContent-Length: 0`r`n`r`n"
    Assert-True ($post.StartsWith("HTTP/1.1 405 Method Not Allowed")) "POST returns 405"

    $traversal = Send-RawRequest "GET /../secret.txt HTTP/1.1`r`nHost: 127.0.0.1`r`n`r`n"
    Assert-True ($traversal.StartsWith("HTTP/1.1 400 Bad Request")) "Directory traversal returns 400"

    $oversized = "GET / HTTP/1.1`r`nHost: 127.0.0.1`r`nX-Large: " + ("a" * 17000) + "`r`n`r`n"
    $oversizedResponse = Send-RawRequest $oversized
    Assert-True ($oversizedResponse.StartsWith("HTTP/1.1 400 Bad Request")) "Oversized header returns 400"

    Test-ParallelClients
    Start-Sleep -Milliseconds 300

    $logLines = Get-Content $ServerLog
    Assert-True (($logLines | Where-Object { $_ -match "\[REQUEST\].*status=200" }).Count -ge 12) `
        "Log records successful requests"
    Assert-True (($logLines | Where-Object { $_ -match "\[REQUEST\].*status=400" }).Count -ge 3) `
        "Log records rejected requests"
    Assert-True (($logLines | Where-Object {
        $_ -notmatch "^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[(SERVER|CONNECT|REQUEST|DISCONNECT|ERROR)\] "
    }).Count -eq 0) "Concurrent log lines remain complete and uncorrupted"

    Stop-TestServer
    $script:ServerProcess = Start-TestServer "restart"
    $restart = Invoke-DemoClient "/hello.txt"
    Assert-True ($restart.Contains("HTTP/1.1 200 OK")) "Server restarts on the same port"

    Write-Host "`nAll $script:Passed smoke checks passed." -ForegroundColor Cyan
}
finally {
    Stop-TestServer
}
