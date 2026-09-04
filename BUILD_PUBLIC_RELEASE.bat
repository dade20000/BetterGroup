@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ================================================
echo   BetterGroup 1.0.0 RC3.2 - PUBLIC RELEASE
echo ================================================
echo.

call BUILD_RELEASE.bat
if errorlevel 1 goto fail

if not exist "RELEASE\BetterGroup_Setup.exe" goto fail
if not exist "RELEASE\BetterGroup_Update.zip" goto fail
if not exist "RELEASE\BetterGroup_Update.exe" goto fail
if not exist "RELEASE\BetterGroup_Uninstall.exe" goto fail

if exist PUBLIC rmdir /S /Q PUBLIC
mkdir PUBLIC
mkdir PUBLIC\GITHUB_RELEASE

copy /Y "RELEASE\BetterGroup_Setup.exe" "PUBLIC\" >nul
copy /Y "RELEASE\BetterGroup_Update.zip" "PUBLIC\GITHUB_RELEASE\" >nul
copy /Y "RELEASE\BetterGroup_Setup.exe" "PUBLIC\GITHUB_RELEASE\" >nul

> "PUBLIC\LEGGIMI.txt" (
echo BetterGroup 1.0.0 RC3.2
echo ========================
echo.
echo PER UN NUOVO UTENTE:
echo   Esegui BetterGroup_Setup.exe
echo.
echo DOPO L'INSTALLAZIONE:
echo   Menu Start ^> BetterGroup ^> Check for updates
echo.
echo Se auto_check=true, BetterGroup controlla anche gli aggiornamenti
echo in modo silenzioso all'accesso a Windows.
echo.
echo Per disinstallare:
echo   Menu Start ^> BetterGroup ^> Uninstall BetterGroup
)

echo.
echo ================================================
echo   PACCHETTO PUBBLICO PRONTO
echo ================================================
echo.
echo DA MANDARE / FAR SCARICARE AGLI UTENTI:
echo   PUBLIC\BetterGroup_Setup.exe
echo.
echo DA CARICARE NELLA GITHUB RELEASE:
echo   PUBLIC\GITHUB_RELEASE\BetterGroup_Setup.exe
echo   PUBLIC\GITHUB_RELEASE\BetterGroup_Update.zip
echo.
pause
exit /b 0

:fail
echo.
echo BUILD PUBBLICA FALLITA.
pause
exit /b 1
