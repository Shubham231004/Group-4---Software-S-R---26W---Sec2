Aircraft-Ground Control Communication System
Sprint 1

Build Requirements:
- Windows
- Visual Studio 2022 or Build Tools with Desktop development with C++
- CMake 3.16+

Build Steps:
1. Open terminal in project root.
2. Run:
   cmake -S . -B build
3. Run:
   cmake --build build

Run Steps:
1. Start server:
   build\Debug\server.exe
   or
   build\server.exe

2. Start client in another terminal:
   build\Debug\client.exe
   or
   build\client.exe

Expected Output:
- server_log.csv created
- client_log.csv created
- Server receives and prints telemetry values
- Client receives acknowledgements