@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo ================================================
echo   BetterGroup 1.0.0 RC3.2 - RELEASE BUILDER
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

if exist build_release rmdir /S /Q build_release
if exist RELEASE rmdir /S /Q RELEASE
mkdir RELEASE

"%CMAKE_EXE%" -S . -B build_release -A x64
if errorlevel 1 goto fail

"%CMAKE_EXE%" --build build_release --config Release
if errorlevel 1 goto fail

rem Create Setup / Update / Uninstall executables first.
call BUILD_EXE.bat
if errorlevel 1 goto fail

if not exist "RELEASE\BetterGroup_Setup.exe" goto fail
if not exist "RELEASE\BetterGroup_Update.exe" goto fail
if not exist "RELEASE\BetterGroup_Uninstall.exe" goto fail

rem ============================================================
rem GitHub updater payload
rem ============================================================
mkdir RELEASE\BetterGroup_Update_Package\payload

copy /Y "build_release\Release\BetterGroup.dll" "RELEASE\BetterGroup_Update_Package\payload\" >nul
copy /Y "BetterGroup_roles.cfg" "RELEASE\BetterGroup_Update_Package\payload\" >nul
copy /Y "version.txt" "RELEASE\BetterGroup_Update_Package\payload\" >nul
copy /Y "RELEASE\BetterGroup_Update.exe" "RELEASE\BetterGroup_Update_Package\payload\" >nul
copy /Y "RELEASE\BetterGroup_Uninstall.exe" "RELEASE\BetterGroup_Update_Package\payload\" >nul

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
 "$p='RELEASE\BetterGroup_Update_Package\payload';" ^
 "$v=(Get-Content 'version.txt' -Raw).Trim();" ^
 "$files=@('BetterGroup.dll','BetterGroup_roles.cfg','version.txt','BetterGroup_Update.exe','BetterGroup_Uninstall.exe');" ^
 "$entries=@();" ^
 "foreach($f in $files){$h=(Get-FileHash (Join-Path $p $f) -Algorithm SHA256).Hash.ToLower();$entries+=@{name=$f;sha256=$h}};" ^
 "@{version=$v;channel='prerelease';files=$entries} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $p 'manifest.json') -Encoding UTF8"
if errorlevel 1 goto fail

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
 "Compress-Archive -Path 'RELEASE\BetterGroup_Update_Package\*' -DestinationPath 'RELEASE\BetterGroup_Update.zip' -Force"
if errorlevel 1 goto fail

if not exist "RELEASE\BetterGroup_Update.zip" goto fail

echo.
echo ================================================
echo   BUILD COMPLETATA
echo ================================================
echo.
echo DA MANDARE AGLI UTENTI:
echo   RELEASE\BetterGroup_Setup.exe
echo.
echo DA CARICARE SU GITHUB RELEASE:
echo   RELEASE\BetterGroup_Setup.exe
echo   RELEASE\BetterGroup_Update.zip
echo.
echo UPDATER LOCALE:
echo   RELEASE\BetterGroup_Update.exe
echo.
echo DISINSTALLAZIONE:
echo   RELEASE\BetterGroup_Uninstall.exe
echo.
pause
exit /b 0

:fail
echo.
echo ================================================
echo   BUILD FALLITA
echo ================================================
echo.
pause
exit /b 1
