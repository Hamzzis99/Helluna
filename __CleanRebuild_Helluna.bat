@echo off
setlocal
title Helluna Clean Rebuild
echo =====================================================
echo   Helluna - CLEAN REBUILD (fix stale UPROPERTY reflection)
echo =====================================================
echo.
set "ENGINE=D:\UnrealServer_Build\Engine"
set "PROJ=D:\UnrealProject\Capston_Project\Helluna"
set "UPROJECT=D:\UnrealProject\Capston_Project\Helluna\Helluna.uproject"
echo [1/4] Closing any running Unreal editors...
taskkill /F /IM UnrealEditor.exe >nul 2>&1
taskkill /F /IM UnrealEditor-Cmd.exe >nul 2>&1
timeout /t 3 /nobreak >nul
echo [2/4] Deleting stale build intermediates...
if exist "%PROJ%\Intermediate\Build" rmdir /S /Q "%PROJ%\Intermediate\Build"
if exist "%PROJ%\Binaries" rmdir /S /Q "%PROJ%\Binaries"
echo [3/4] Rebuilding HellunaEditor (Development, Win64). This can take a few minutes...
call "%ENGINE%\Build\BatchFiles\Build.bat" HellunaEditor Win64 Development -Project="%UPROJECT%" -WaitMutex
set ERR=%ERRORLEVEL%
echo.
if "%ERR%"=="0" (
  echo =====================================================
  echo   [4/4] BUILD SUCCEEDED.  Now open Helluna.uproject.
  echo =====================================================
) else (
  echo =====================================================
  echo   [4/4] BUILD FAILED  ^(code %ERR%^).  Scroll up to the FIRST error.
  echo =====================================================
)
echo.
pause
