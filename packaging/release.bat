@echo off
REM Release bauen: frischer Release-Build, alle Tests, dann die EXE samt SHA-256
REM nach dist\. Getaggt und auf GitHub hochgeladen wird von Hand:
REM
REM   Tag      KERS_SubsystemsV<version>   (Version aus static\version.txt)
REM   Asset    dist\KERS_Subsystems.exe    (genau dieser Name - der Updater sucht ihn)
REM
REM GitHub rechnet den SHA-256 des Assets selbst aus und gibt ihn in der API als
REM "digest" heraus; der Updater prueft dagegen. Die .sha256-Datei hier ist nur
REM zum Vergleichen von Hand.

setlocal
call "%~dp0env.bat" >nul
cd /d "%~dp0.."

set /p VERSION=<static\version.txt
echo === KERS Subsystems %VERSION%

echo === Build
call "%~dp0build.bat" Release
if errorlevel 1 (
    echo Build fehlgeschlagen.
    exit /b 1
)

echo === Tests
ctest --test-dir build --output-on-failure
if errorlevel 1 (
    echo Tests fehlgeschlagen - kein Release.
    exit /b 1
)

echo === dist\
if not exist dist mkdir dist
copy /y build\KERS_Subsystems.exe dist\KERS_Subsystems.exe >nul
if errorlevel 1 (
    echo Kopieren fehlgeschlagen.
    exit /b 1
)
for /f "tokens=* delims=" %%H in ('certutil -hashfile dist\KERS_Subsystems.exe SHA256 ^| findstr /v ":"') do (
    set "HASH=%%H"
    goto :hash_da
)
:hash_da
set "HASH=%HASH: =%"
echo %HASH% *KERS_Subsystems.exe> dist\KERS_Subsystems.exe.sha256

for %%F in (dist\KERS_Subsystems.exe) do set "SIZE=%%~zF"
echo.
echo Fertig:   dist\KERS_Subsystems.exe
echo Groesse:  %SIZE% Bytes
echo SHA-256:  %HASH%
echo Tag:      KERS_SubsystemsV%VERSION%
exit /b 0
