@echo off
REM Baut KERS_Subsystems.exe (Release) mit der portablen Toolchain aus env.bat.
REM
REM   packaging\build.bat            Release  -> build\KERS_Subsystems.exe
REM   packaging\build.bat Debug      Debug    -> build-debug\KERS_Subsystems.exe
REM
REM Voraussetzung: statisches Qt unter D:\Qt\6.11.2-static (tools\build_qt_static.cmd).

setlocal
call "%~dp0env.bat"
cd /d "%~dp0.."

set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
set "OUT=build"
if /I "%CFG%"=="Debug" set "OUT=build-debug"

echo === CMake configure (%CFG%)
cmake -S . -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% -DCMAKE_PREFIX_PATH=%QT_STATIC:\=/%
if errorlevel 1 exit /b 1

echo === Build
cmake --build "%OUT%"
if errorlevel 1 exit /b 1

echo.
echo Fertig: %OUT%\KERS_Subsystems.exe
for %%F in ("%OUT%\KERS_Subsystems.exe") do echo Groesse: %%~zF Bytes
exit /b 0
