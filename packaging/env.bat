@echo off
REM Build-Umgebung fuer KERS Subsystems (C++).
REM
REM Nichts davon ist systemweit installiert: Compiler (LLVM-MinGW), CMake und Ninja
REM liegen portabel unter D:\Tools, das statisch gebaute Qt unter D:\Qt. Dieses
REM Skript setzt nur den PATH der aktuellen Konsole - in jeder neuen Konsole einmal
REM "call packaging\env.bat" ausfuehren, dann bauen.
REM
REM Warum LLVM-MinGW statt Visual Studio Build Tools: die Build Tools legen ihr
REM Windows SDK zwingend auf C: ab, und auf C: soll nichts installiert werden.

set "KERS_TOOLS=D:\Tools"
set "QT_STATIC=D:\Qt\6.11.2-static"

set "PATH=%KERS_TOOLS%\llvm-mingw\bin;%KERS_TOOLS%\cmake\bin;%KERS_TOOLS%\ninja;%QT_STATIC%\bin;%PATH%"
set "CC=clang"
set "CXX=clang++"

echo [env] Compiler:  %KERS_TOOLS%\llvm-mingw
echo [env] CMake:     %KERS_TOOLS%\cmake
echo [env] Ninja:     %KERS_TOOLS%\ninja
echo [env] Qt:        %QT_STATIC%
