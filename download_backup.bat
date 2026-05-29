@echo off
title VitaVault - Download Backup from PS Vita
setlocal enabledelayedexpansion

set VITA_IP=192.168.1.18
set FTP_PORT=1337
set LOCAL_DIR=C:\VitaVault_Backups
set REMOTE_DIR=/ux0:data/VitaVault

:MENU
cls
echo ============================================
echo    VitaVault - Download Backup from PS Vita
echo ============================================
echo.
echo Vita IP : %VITA_IP%
echo Port    : %FTP_PORT%
echo Remote  : %REMOTE_DIR%
echo Save to : %LOCAL_DIR%
echo.
echo 1) Download ALL backups
echo 2) Download specific backup (enter name)
echo 3) Download with Windows FTP
echo 4) Change Vita IP
echo 5) Change remote folder (Vita path)
echo 6) Change local folder (PC path)
echo 7) Exit
echo.
set /p CHOICE=Choose (1-7): 

if "%CHOICE%"=="1" goto ALL
if "%CHOICE%"=="2" goto SPECIFIC
if "%CHOICE%"=="3" goto FTPMAN
if "%CHOICE%"=="4" goto IP
if "%CHOICE%"=="5" goto REMOTE
if "%CHOICE%"=="6" goto LOCAL
if "%CHOICE%"=="7" exit /b
goto MENU

:IP
cls
echo.
set /p VITA_IP=Enter Vita IP: 
goto MENU

:REMOTE
cls
echo.
echo Current remote folder: %REMOTE_DIR%
echo Enter the remote backup folder path (e.g. /ux0:data/VitaVault)
echo If you changed destination on Vita, enter it here.
echo.
set /p REMOTE_DIR=Remote path: 
goto MENU

:LOCAL
cls
echo.
echo Current local folder: %LOCAL_DIR%
echo Enter the local folder path on your PC.
echo You can use any drive, e.g.:
echo   D:\VitaVault_Backups
echo   E:\Backup
echo   F:\
echo.
set /p LOCAL_DIR=Local path: 
if "%LOCAL_DIR%"=="" goto MENU
goto MENU

:SPECIFIC
cls
echo ============================================
echo    Download specific backup
echo ============================================
echo.

where wget >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo wget not found.
    pause
    goto MENU
)
echo wget found!
echo.
echo Enter backup folder name (e.g. 2026-05-29_18-00-00)
echo.
set /p LATEST=Name: 
if "%LATEST%"=="" goto MENU

if not exist "%LOCAL_DIR%" mkdir "%LOCAL_DIR%"

cls
echo ============================================
echo    Downloading: %LATEST%
echo ============================================
echo.
wget -r -nH --no-parent --progress=bar:force --timeout=15 --tries=2 --ftp-user=anonymous --ftp-password=vita@ftp "ftp://%VITA_IP%:%FTP_PORT%%REMOTE_DIR%/%LATEST%/" -P "%LOCAL_DIR%"

echo.
echo Exit code: %ERRORLEVEL%
dir "%LOCAL_DIR%" /b 2>nul
echo.
pause
goto MENU

:ALL
cls
echo ============================================
echo    Download ALL backups
echo ============================================
echo.

where wget >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo wget not found.
    pause
    goto MENU
)
echo wget found!

if not exist "%LOCAL_DIR%" mkdir "%LOCAL_DIR%"

cls
echo ============================================
echo    Downloading ALL backups from %REMOTE_DIR%
echo ============================================
echo.
wget -r -nH --no-parent --progress=bar:force --timeout=15 --tries=2 --ftp-user=anonymous --ftp-password=vita@ftp "ftp://%VITA_IP%:%FTP_PORT%%REMOTE_DIR%/" -P "%LOCAL_DIR%"

echo.
echo Exit code: %ERRORLEVEL%
dir "%LOCAL_DIR%" /b 2>nul
echo.
pause
goto MENU

:FTPMAN
cls
echo ============================================
echo    Windows FTP Client
echo ============================================
echo.
echo Commands:
echo   dir                         - list files
echo   cd %REMOTE_DIR%            - go to backups folder
echo   dir                         - list backups
echo   cd BACKUPNAME              - enter a backup
echo   binary                      - binary mode
echo   prompt                      - disable prompts
echo   mget *                      - download all
echo   quit                        - exit
echo.
pause
if not exist "%LOCAL_DIR%" mkdir "%LOCAL_DIR%"
(
echo open %VITA_IP% %FTP_PORT%
echo anonymous
echo vita@ftp
echo binary
echo prompt
echo lcd "%LOCAL_DIR%"
echo cd %REMOTE_DIR%
echo dir
) > "%TEMP%\ftpscript.txt"
start ftp -s:"%TEMP%\ftpscript.txt"
goto MENU