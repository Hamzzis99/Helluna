@echo off
setlocal EnableDelayedExpansion
title Helluna CLEAN Rebuild v2
set "ENGINE=D:\UnrealServer_Build\Engine"
set "PROJ=D:\UnrealProject\Capston_Project\Helluna"
set "UPROJECT=D:\UnrealProject\Capston_Project\Helluna\Helluna.uproject"
echo =====================================================
echo   Helluna CLEAN REBUILD v2  (waits for full exit)
echo =====================================================
echo.
echo [1/5] Killing Unreal editors...
taskkill /F /IM UnrealEditor.exe >nul 2>&1
taskkill /F /IM UnrealEditor-Cmd.exe >nul 2>&1
echo [2/5] Waiting until UnrealEditor.exe is FULLY gone...
:WAITLOOP
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
  echo     still alive, waiting 2s...
  timeout /t 2 /nobreak >nul
  goto WAITLOOP
)
echo     editor fully closed.
timeout /t 2 /nobreak >nul
echo [3/5] Deleting Intermediate\Build and Binaries...
rmdir /S /Q "%PROJ%\Intermediate\Build" 2>nul
rmdir /S /Q "%PROJ%\Binaries" 2>nul
if exist "%PROJ%\Intermediate\Build" (
  echo     [ERROR] Could NOT delete Intermediate\Build - still locked. Close ALL Unreal/Rider and retry.
  pause
  exit /b 1
)
echo     clean OK - intermediates deleted.
echo [4/5] Rebuilding HellunaEditor ^(Development Win64^)...
call "%ENGINE%\Build\BatchFiles\Build.bat" HellunaEditor Win64 Development -Project="%UPROJECT%" -WaitMutex
set ERR=%ERRORLEVEL%
echo.
if "%ERR%"=="0" (
  echo =====================================================
  echo   [5/5] BUILD SUCCEEDED.  Now open Helluna.uproject.
  echo =====================================================
) else (
  echo =====================================================
  echo   [5/5] BUILD FAILED ^(code %ERR%^). Scroll up to FIRST error.
  echo =====================================================
)
echo.
pause
