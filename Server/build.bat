@echo off
cd %~dp0
IF not exist build/ mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="D:\Code\vcpkg-master\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
pause