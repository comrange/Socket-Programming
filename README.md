# SocketDemo

Đồ án Web Client - Web Server bằng C++17, Winsock2 và HTTP/1.1 rút gọn. Server phục vụ file tĩnh, xử lý nhiều kết nối đồng thời; Client tự tạo HTTP GET request và hiển thị response nhận qua TCP socket.

## Chức năng đã hoàn thành

- Hai chương trình riêng: `socket_server.exe` và `socket_client.exe`.
- Server mặc định nghe tại `127.0.0.1:8080`, có thể bật cho mạng LAN bằng tham số.
- Mỗi client được xử lý trong một thread riêng, không chặn vòng lặp nhận kết nối mới.
- Hỗ trợ HTTP `GET` và các response `200`, `400`, `404`, `405`.
- Gửi đúng `Content-Length`, `Content-Type` và `Connection: close`.
- Giới hạn request header 16 KB và chặn đường dẫn có khả năng directory traversal.
- Log thread-safe ra console và `server.log`, gồm client, request, trạng thái, số byte, thời gian xử lý và số kết nối đang hoạt động.
- Có smoke test tự động cho luồng HTTP, bảo mật đường dẫn, khởi động lại và 10 client đồng thời.
- Có website mẫu để demo bằng Client tự viết hoặc trình duyệt.

## Kiến trúc

```text
Client/Browser
      |
      | TCP + HTTP/1.1
      v
Listening Socket -> accept() -> một thread cho mỗi client
                              -> phân tích HTTP request
                              -> đọc file trong www
                              -> gửi HTTP response
```

Phần `src/common` chứa vòng đời Winsock, socket RAII, gửi dữ liệu đầy đủ và xử lý HTTP dùng chung. Giao diện console có thể được thay bằng MFC sau này mà không cần viết lại lõi này.

## Build trên Windows

Yêu cầu CMake, Ninja và MinGW có hỗ trợ Winsock2.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build
```

Hai file chạy và thư mục `www` sẽ nằm trong `build`.

## Chạy trên một máy

Terminal thứ nhất:

```powershell
cd build
./socket_server.exe
```

Terminal thứ hai:

```powershell
cd build
./socket_client.exe 127.0.0.1 8080 /index.html
./socket_client.exe 127.0.0.1 8080 /hello.txt
./socket_client.exe 127.0.0.1 8080 /missing.txt
```

Có thể mở `http://127.0.0.1:8080/index.html` bằng trình duyệt.

## Chạy qua mạng LAN

Khởi động Server và cho phép nhận kết nối trên tất cả card mạng:

```powershell
cd build
./socket_server.exe 8080 www 0.0.0.0
```

Tìm địa chỉ IPv4 của máy Server:

```powershell
ipconfig
```

Trên máy khác cùng mạng, thay `<SERVER_IP>` bằng địa chỉ vừa tìm được:

```powershell
./socket_client.exe <SERVER_IP> 8080 /index.html
```

Hoặc mở `http://<SERVER_IP>:8080/index.html` bằng trình duyệt. Nếu Windows Firewall hỏi quyền truy cập, chỉ cho phép trên mạng riêng đáng tin cậy. Dự án không tự tạo hay thay đổi firewall rule.

## Tham số chương trình

```text
socket_server [port] [document_root] [bind_address]
socket_client <host> <port> <path>
```

Ví dụ đổi cổng, thư mục website và vẫn chỉ nghe localhost:

```powershell
./socket_server.exe 9000 ../www 127.0.0.1
```

## Chạy kiểm thử

Sau khi build:

```powershell
powershell -ExecutionPolicy Bypass -File tests/smoke_test.ps1
```

Test sử dụng cổng `18080`, tự khởi động/dừng Server và không thay đổi cấu hình hệ thống. Có thể truyền build directory hoặc cổng khác:

```powershell
./tests/smoke_test.ps1 -BuildDir ./build -Port 19090
```

## Kịch bản demo 4 phút

1. Khởi động Server tại localhost và chỉ ra bind address, document root trong log.
2. Dùng Client lấy `/index.html`, sau đó thử `/missing.txt` để minh họa `200` và `404`.
3. Chạy smoke test và chỉ ra dòng xác nhận 10 kết nối hoạt động đồng thời.
4. Khởi động lại với bind address `0.0.0.0`, truy cập từ máy khác hoặc trình duyệt.
5. Mở `server.log` để giải thích request id, client endpoint, status và thời gian xử lý.

## Giới hạn hiện tại

- Server dùng mô hình một thread cho mỗi client và chưa giới hạn số thread.
- Server dừng bằng `Ctrl+C`, chưa có graceful shutdown.
- Chỉ hỗ trợ `GET`, HTTP/1.1 và đóng kết nối sau mỗi response.
- Client hiển thị body ra console, chưa lưu file nhị phân.
- Chưa có MFC, HTTPS, xác thực, `POST`, upload/download hay keep-alive.

## Hướng nâng cấp

- Chuyển Client và Server sang MFC, dùng `WSAAsyncSelect` để tránh treo giao diện.
- Thay thread-per-client bằng thread pool có giới hạn và hàng đợi công việc.
- Thêm `HEAD`, `POST`, upload/download, keep-alive và cache.
- Thêm graceful shutdown, cấu hình bằng file và log rotation.
- Bổ sung unit test cho HTTP parser, kiểm thử tải và báo cáo kiến trúc.

## Tài liệu tham khảo

Thiết kế dựa trên tài liệu Winsock, HTTP và các ví dụ Client-Server trong thư mục `Ref`. Mã nguồn của bản demo được tổ chức lại theo kiến trúc riêng ở trên.
