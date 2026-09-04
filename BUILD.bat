@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo ================================================
echo   BetterGroup v1.0.0 RC3.3.5
echo ================================================
echo.

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"

if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
 set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
 set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

if not defined CMAKE_EXE (
 echo [ERRORE] CMake non trovato.
 pause
 exit /b 1
)

set "GENERATOR="

if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community" (
 set "GENERATOR=Visual Studio 18 2026"
)

if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\17\Community" (
 set "GENERATOR=Visual Studio 17 2022"
)

if defined GENERATOR (
 "%CMAKE_EXE%" -S . -B build -G "!GENERATOR!" -A x64
) else (
 "%CMAKE_EXE%" -S . -B build -A x64
)

if errorlevel 1 goto fail

"%CMAKE_EXE%" --build build --config Release

if errorlevel 1 goto fail

if not exist dist mkdir dist

copy /Y "build\Release\BetterGroup.dll" "dist\" >nul
copy /Y "BetterGroup_BetterGroup_roles.cfg" "dist\" >nul
copy /Y "BetterGroup.cfg" "dist\" >nul

echo.
echo ================================================
echo BUILD RIUSCITA
echo ================================================
echo.
echo In plugins metti SOLO:
echo   BetterGroup.dll
echo   BetterGroup_roles.cfg
echo.
echo Togli le vecchie TMPStaffShield*.dll / BetterGroup.dll prima di sostituire
echo.
echo Nella skin lascia:
echo   truckersmp_staff_MARKER.png
echo rinominato:
echo   truckersmp_staff.png
echo.
pause
exit /b 0

:fail
echo.
echo ================================================
echo BUILD FALLITA
echo ================================================
pause
exit /b 1
