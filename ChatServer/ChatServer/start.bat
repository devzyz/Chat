@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\scripts\windows-local.ps1" -Task GenerateProtocols
if errorlevel 1 exit /b %errorlevel%
echo Canonical protocol sources generated.
