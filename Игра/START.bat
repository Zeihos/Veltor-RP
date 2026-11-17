@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0SETUP-AND-START.ps1"
if errorlevel 1 (
 echo.
 echo Failed. See BUILD-LOG.txt if compilation started.
 pause
)
