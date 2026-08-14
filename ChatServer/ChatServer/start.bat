@echo off
if not defined VCPKG_TARGET_TRIPLET set VCPKG_TARGET_TRIPLET=x64-windows-chat
set "VCPKG_INSTALLED_DIR=%~dp0..\..\vcpkg_installed\%VCPKG_TARGET_TRIPLET%"
set "PROTOC_PATH=%VCPKG_INSTALLED_DIR%\tools\protobuf\protoc.exe"
set "GRPC_PLUGIN_PATH=%VCPKG_INSTALLED_DIR%\tools\grpc\grpc_cpp_plugin.exe"
set "PROTO_FILE=message.proto"

echo Generating gRPC code ...
%PROTOC_PATH% -I="." --grpc_out="." --plugin=protoc-gen-grpc="%GRPC_PLUGIN_PATH%" "%PROTO_FILE%"

echo Generating C++ code ...
%PROTOC_PATH% --cpp_out=. "%PROTO_FILE%"

echo Done.
