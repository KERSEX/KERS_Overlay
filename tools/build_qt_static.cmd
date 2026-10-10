@echo off
REM Baut Qt 6.11.2 STATISCH aus dem Quelltext - einmalig, dauert gut eine Stunde.
REM
REM   tools\build_qt_static.cmd            alle vier Stufen
REM   tools\build_qt_static.cmd base       nur qtbase
REM   tools\build_qt_static.cmd shader     nur qtshadertools   (braucht installiertes qtbase)
REM   tools\build_qt_static.cmd decl       nur qtdeclarative   (braucht qtbase UND qtshadertools)
REM   tools\build_qt_static.cmd ws         nur qtwebsockets    (ab 0.3.1: OBS-Steuerung, braucht qtbase)
REM
REM Reihenfolge ist Pflicht: qtdeclarative baut Qt Quick nur, wenn das Werkzeug
REM "qsb" aus qtshadertools schon installiert ist - sonst baut es STILL ohne
REM QtQuick, ohne MultiEffect, ohne Shapes.
REM
REM Quellen: D:\Qt\src\<modul>-everywhere-src-6.11.2   (ZIPs von download.qt.io)
REM Build:   D:\Qt\b\<stufe>                            (nach dem Bau loeschbar, 10-15 GB)
REM Ziel:    D:\Qt\6.11.2-static                        (~2,5 GB)
REM
REM Wichtige configure-Schalter:
REM   -static -static-runtime   eine EXE ohne Qt-DLLs und ohne libc++/winpthread-DLLs
REM   -platform win32-clang-g++ LLVM-MinGW (clang + lld), siehe packaging\env.bat
REM   -schannel                 HTTPS ueber Windows-Bordmittel (Updater gegen GitHub), kein OpenSSL
REM   -qt-zlib ...              alle Drittbibliotheken aus dem Qt-Baum, keine Systemabhaengigkeiten

setlocal
call "%~dp0..\packaging\env.bat"

set "SRC=D:\Qt\src"
set "B=D:\Qt\b"
set "PREFIX=D:\Qt\6.11.2-static"
set "VER=6.11.2"
set "STAGE=%~1"
if "%STAGE%"=="" set "STAGE=all"

where clang++ >nul 2>nul || (echo [FEHLER] clang++ nicht gefunden - liegt LLVM-MinGW unter D:\Tools\llvm-mingw? & exit /b 2)
where cmake   >nul 2>nul || (echo [FEHLER] cmake nicht gefunden & exit /b 2)
where ninja   >nul 2>nul || (echo [FEHLER] ninja nicht gefunden & exit /b 2)

if /I "%STAGE%"=="all"    goto base
if /I "%STAGE%"=="base"   goto base
if /I "%STAGE%"=="shader" goto shader
if /I "%STAGE%"=="decl"   goto decl
if /I "%STAGE%"=="ws"     goto ws
echo [FEHLER] Unbekannte Stufe "%STAGE%" (all, base, shader, decl, ws)
exit /b 2

:base
echo === [%DATE% %TIME%] qtbase: configure
if not exist "%B%\base" mkdir "%B%\base"
pushd "%B%\base"
call "%SRC%\qtbase-everywhere-src-%VER%\configure.bat" -prefix "%PREFIX%" -release -static -static-runtime -platform win32-clang-g++ -nomake examples -nomake tests -qt-zlib -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz -qt-pcre -schannel -no-feature-openssl -no-dbus -no-sql-sqlite -no-sql-odbc
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtbase: build
cmake --build . --parallel
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtbase: install
cmake --install .
if errorlevel 1 goto fehler
popd
if /I "%STAGE%"=="base" goto fertig

:shader
echo === [%DATE% %TIME%] qtshadertools: configure
if not exist "%B%\shader" mkdir "%B%\shader"
pushd "%B%\shader"
call "%PREFIX%\bin\qt-configure-module.bat" "%SRC%\qtshadertools-everywhere-src-%VER%"
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtshadertools: build
cmake --build . --parallel
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtshadertools: install
cmake --install .
if errorlevel 1 goto fehler
popd
if /I "%STAGE%"=="shader" goto fertig

:decl
echo === [%DATE% %TIME%] qtdeclarative: configure
if not exist "%B%\decl" mkdir "%B%\decl"
pushd "%B%\decl"
call "%PREFIX%\bin\qt-configure-module.bat" "%SRC%\qtdeclarative-everywhere-src-%VER%"
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtdeclarative: build
cmake --build . --parallel
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtdeclarative: install
cmake --install .
if errorlevel 1 goto fehler
popd
if /I "%STAGE%"=="decl" goto fertig

:ws
echo === [%DATE% %TIME%] qtwebsockets: configure
if not exist "%B%\ws" mkdir "%B%\ws"
pushd "%B%\ws"
call "%PREFIX%\bin\qt-configure-module.bat" "%SRC%\qtwebsockets-everywhere-src-%VER%"
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtwebsockets: build
cmake --build . --parallel
if errorlevel 1 goto fehler
echo === [%DATE% %TIME%] qtwebsockets: install
cmake --install .
if errorlevel 1 goto fehler
popd

:fertig
echo === [%DATE% %TIME%] FERTIG
echo.
echo Pruefen:
echo   %PREFIX%\qml\QtQuick\Effects\      (MultiEffect)
echo   %PREFIX%\qml\QtQuick\Shapes\       (Shapes)
echo   %PREFIX%\plugins\platforms\        (qwindows)
echo   %PREFIX%\plugins\tls\              (qschannelbackend)
echo   %PREFIX%\include\QtZlib\zlib.h     (gzip fuer .f1rec)
exit /b 0

:fehler
set "RC=%ERRORLEVEL%"
popd 2>nul
echo === [%DATE% %TIME%] FEHLER in Stufe "%STAGE%" (Exit %RC%)
exit /b 1
