@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ================================================
echo   BetterGroup - EXE BUILDER
echo ================================================
echo.

set "CSC="

if exist "%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" (
    set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
)

if not defined CSC if exist "%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe" (
    set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)

if not defined CSC (
    for /f "delims=" %%I in ('where csc.exe 2^>nul') do if not defined CSC set "CSC=%%I"
)

if not defined CSC if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe" (
    set "CSC=%ProgramFiles%\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
)

if not defined CSC if exist "%ProgramFiles%\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\Roslyn\csc.exe" (
    set "CSC=%ProgramFiles%\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
)

if not defined CSC (
    echo [ERRORE] csc.exe non trovato.
    echo Installa .NET Framework Developer Pack oppure Visual Studio con .NET build tools.
    exit /b 1
)

echo C# compiler:
echo   "%CSC%"
echo.

if not exist "build_release\Release\BetterGroup.dll" (
    echo [ERRORE] BetterGroup.dll non trovato.
    echo Esegui BUILD_RELEASE.bat, non BUILD_EXE.bat da solo.
    exit /b 1
)

if not exist RELEASE mkdir RELEASE

rem ------------------------------------------------
rem BetterGroup_Update.exe
rem ------------------------------------------------
"%CSC%" /nologo /target:winexe /platform:anycpu ^
 /reference:System.Windows.Forms.dll ^
 /out:"RELEASE\BetterGroup_Update.exe" ^
 /resource:"setup\updater.ps1",BetterGroup.updater.ps1 ^
 "setup\BetterGroupUpdateWrapper.cs"

if errorlevel 1 goto fail
if not exist "RELEASE\BetterGroup_Update.exe" goto fail

rem ------------------------------------------------
rem BetterGroup_Uninstall.exe
rem ------------------------------------------------
"%CSC%" /nologo /target:winexe /platform:anycpu ^
 /reference:System.Windows.Forms.dll ^
 /out:"RELEASE\BetterGroup_Uninstall.exe" ^
 /resource:"setup\uninstaller.ps1",BetterGroup.uninstaller.ps1 ^
 "setup\BetterGroupUninstallWrapper.cs"

if errorlevel 1 goto fail
if not exist "RELEASE\BetterGroup_Uninstall.exe" goto fail

rem ------------------------------------------------
rem BetterGroup_Setup.exe
rem ------------------------------------------------
"%CSC%" /nologo /target:winexe /platform:anycpu ^
 /reference:System.Windows.Forms.dll ^
 /out:"RELEASE\BetterGroup_Setup.exe" ^
 /resource:"setup\installer.ps1",BetterGroup.installer.ps1 ^
 /resource:"build_release\Release\BetterGroup.dll",BetterGroup.dll ^
 /resource:"BetterGroup_roles.cfg",BetterGroup.roles ^
 /resource:"BetterGroup.cfg",BetterGroup.config ^
 /resource:"version.txt",BetterGroup.version ^
 /resource:"setup\BetterGroup_update.ini",BetterGroup.updateini ^
 /resource:"RELEASE\BetterGroup_Update.exe",BetterGroup.updateexe ^
 /resource:"RELEASE\BetterGroup_Uninstall.exe",BetterGroup.uninstallexe ^
 "setup\BetterGroupSetupWrapper.cs"

if errorlevel 1 goto fail
if not exist "RELEASE\BetterGroup_Setup.exe" goto fail

echo.
echo ================================================
echo   EXE CREATI CORRETTAMENTE
echo ================================================
echo.
dir /b "RELEASE\BetterGroup_*.exe"
echo.
exit /b 0

:fail
echo.
echo ================================================
echo   [ERRORE] CREAZIONE EXE FALLITA
echo ================================================
echo.
echo Nessun falso "BUILD COMPLETATA": il builder ora
echo controlla fisicamente che gli EXE esistano.
echo.
exit /b 1
