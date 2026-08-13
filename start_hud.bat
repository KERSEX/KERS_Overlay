@echo off
REM KERS HUD - Overlay als Fenster ueber dem Spiel + Subsystems-Schaltbrett.
REM Den Server kannst du danach im Schaltbrett per Knopf starten.
REM
REM Dass sich dieses Fenster gleich wieder schliesst, ist normal: das HUD laeuft
REM mit pythonw.exe weiter, also ohne eigene Konsole. Bedient wird es ueber das
REM Subsystems-Fenster und das Tray-Icon unten rechts.
title KERS HUD

cd /d "%~dp0"

if not exist "venv\Scripts\pythonw.exe" (
    echo [FEHLER] Keine venv gefunden in "%CD%\venv".
    echo          Bitte einmal run.bat starten - das legt sie an.
    echo.
    pause
    exit /b 1
)

venv\Scripts\python.exe -c "import PySide6" 2>nul
if errorlevel 1 (
    echo [INFO] PySide6 fehlt noch, wird installiert ^(~80 MB, dauert einmalig^)...
    venv\Scripts\python.exe -m pip install PySide6
    if errorlevel 1 (
        echo.
        echo [FEHLER] Installation fehlgeschlagen.
        pause
        exit /b 1
    )
)

REM Alte Fehlermeldung wegraeumen, damit unten nur ein FRISCHER Absturz anschlaegt.
if exist "hud\hud_error.log" del "hud\hud_error.log"

start "" venv\Scripts\pythonw.exe hud\kers_hud.py %*

REM Kurz warten und nachsehen, ob es beim Start gestolpert ist. Ohne das waere ein
REM Fehler unsichtbar - pythonw hat keine Konsole, in die er schreiben koennte.
timeout /t 4 /nobreak >nul
if exist "hud\hud_error.log" (
    echo.
    echo [FEHLER] Das HUD ist beim Start abgebrochen:
    echo.
    type "hud\hud_error.log"
    echo.
    pause
    exit /b 1
)
