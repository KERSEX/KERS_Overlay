"""
KERS HUD - Startpunkt
=====================

Zeigt das Overlay als IMMER-IM-VORDERGRUND-Fenster auf dem Desktop, statt es nur
als Browser-Quelle in OBS zu haben. Dazu ein kleines Schaltbrett.

ZWEI RENDERER
-------------
    qml   (Vorgabe)  Die Overlay-Szene direkt in QML - kein Chromium, echter
                     Alphakanal, deutlich sparsamer. Die Bausteine sind hier EINE
                     Szene; was sichtbar ist, steht in /settings.
    web              Das alte Verhalten: die HTML-Seiten aus templates/ in einer
                     QWebEngineView. Bleibt, solange in QML noch nicht alles
                     portiert ist, und als Vergleichsmoeglichkeit.

Umschalten per --web / --qml oder im Schaltbrett (wirkt beim naechsten Start).

Zwei Fenster:

    HUD-Fenster   rahmenlos, immer oben, durchsichtiger Hintergrund.
                  Gesperrt gehen Klicks durch -> stoert beim Fahren nicht.

    Subsystems    normales Fenster, NUR was es sonst nirgends gibt: den Server
                  (main.py) starten, HUD an/aus, Bildschirm fuellen, sperren,
                  platzieren. Bausteine und alle Einstellungen bleiben in
                  /settings und /regie - dorthin fuehren zwei Knoepfe.

Dazu ein Tray-Icon unten rechts - noetig, weil das gesperrte HUD ja keine Klicks
mehr annimmt und das Schaltbrett auch mal zugeklappt ist.

Start:
    ..\\venv\\Scripts\\pythonw.exe kers_hud.py         (oder start_hud.bat im Hauptordner)

Optionen:
    --url http://127.0.0.1:5100   anderer Server
    --qml / --web                 Renderer waehlen (Vorgabe: der gemerkte, sonst qml)
    --demo [race|quali]           erfundenes Rennen statt Server - zum Bauen am Overlay
    --chroma                      OBS-Rueckfallebene: einfarbiger Hintergrund
    --page /test                  Spielwiese statt Produktion (nur --web)
    --unlocked                    entsperrt starten
    --no-panel                    ohne Schaltbrett starten
    --no-gpu                      falls der Hintergrund schwarz statt durchsichtig bleibt

Voraussetzung: main.py laeuft (run.bat im Hauptordner) - ausser mit --demo.
"""

import argparse
import os
import sys

try:
    from PySide6.QtGui import QAction
    from PySide6.QtWidgets import QApplication, QMenu, QSystemTrayIcon
except ImportError as exc:  # pragma: no cover - reine Startdiagnose
    print("Fehlende Abhaengigkeit:", exc)
    print()
    print("PySide6 gehoert in dieselbe venv wie der Server:")
    print("    run.bat einmal starten  (installiert requirements.txt inkl. PySide6)")
    print("oder von Hand:")
    print("    venv\\Scripts\\python.exe -m pip install PySide6")
    sys.exit(1)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from api_client import ApiClient          # noqa: E402
from common import load_state, make_icon, migrate_data, save_state  # noqa: E402
from subsystems_panel import SubsystemsPanel  # noqa: E402

# overlay_window (QtWebEngine) und qml_overlay werden erst in main() geladen -
# je nachdem, welcher Renderer laeuft. Beides oben zu importieren hiesse, immer
# Chromium mit hochzuziehen, auch wenn nur die QML-Szene gebraucht wird.

# Als EXE liegt __file__ im Pyinstaller-Temp-Ordner, der beim Beenden geloescht
# wird. Der Absturz-Log muss neben der EXE liegen, sonst ist er nach dem Schliessen weg.
if getattr(sys, "frozen", False):
    ERROR_LOG = os.path.join(os.path.dirname(sys.executable), "hud_error.log")
else:
    ERROR_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hud_error.log")


def _install_crash_log() -> None:
    """Abstuerze sichtbar machen.

    start_hud.bat startet mit pythonw.exe, also OHNE Konsole - ein Traceback
    ginge sonst spurlos ins Nichts und das HUD "geht einfach nicht auf".
    Deshalb: in hud_error.log schreiben und, wenn moeglich, einen Dialog zeigen.
    """
    import traceback

    def hook(exc_type, exc, tb):
        text = "".join(traceback.format_exception(exc_type, exc, tb))
        try:
            with open(ERROR_LOG, "w", encoding="utf-8") as f:
                f.write(text)
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        sys.__excepthook__(exc_type, exc, tb)
        try:
            from PySide6.QtWidgets import QApplication, QMessageBox
            if QApplication.instance():
                QMessageBox.critical(
                    None, "KERS HUD - Fehler",
                    f"Das HUD ist gestolpert:\n\n{exc}\n\nDetails in:\n{ERROR_LOG}")
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    sys.excepthook = hook


def _claim_taskbar_identity() -> None:
    """Eigene Identitaet in der Taskleiste anmelden.

    ⚠ Ohne das zeigt die Taskleiste beim Start ueber python.exe/pythonw.exe das
    PYTHON-Symbol statt unseres Blitzes - und zwar unabhaengig davon, was
    setWindowIcon() sagt. Windows gruppiert Fenster naemlich nach der
    "Application User Model ID", und die erbt ein Skript vom Interpreter, der es
    startet. Erst eine eigene Id macht aus dem Prozess ein eigenes Programm mit
    eigenem Symbol.

    Als fertige EXE braucht es das nicht - da ist die EXE selbst die Identitaet.
    Es schadet aber auch nicht, deshalb steht hier keine Sonderbehandlung.
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(
            "KERS.Overlay.HUD")
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(f"[HUD] Taskleisten-Identitaet nicht setzbar: {e}")


def build_tray(app: QApplication, hud, panel) -> QSystemTrayIcon:
    """Tray-Menue - die Notbremse, wenn das HUD gesperrt und die Steuerung zu ist."""
    icon = make_icon()
    tray = QSystemTrayIcon(icon, app)
    tray.setToolTip("KERS HUD")
    menu = QMenu()

    act_panel = QAction("Subsystems anzeigen", menu)
    act_panel.triggered.connect(lambda: (panel.show(), panel.raise_(),
                                         panel.activateWindow()) if panel else None)
    menu.addAction(act_panel)
    if panel is not None:
        menu.addAction(QAction("Server starten", menu, triggered=panel.start_server))
        menu.addAction(QAction("Server stoppen", menu, triggered=lambda: panel.stop_server()))
    menu.addSeparator()

    act_show = QAction("HUD anzeigen", menu, checkable=True)
    act_show.setChecked(hud.state["visible"])
    act_show.triggered.connect(hud.set_hud_visible)
    hud.hudVisibilityChanged.connect(act_show.setChecked)
    menu.addAction(act_show)

    act_lock = QAction("Gesperrt (klick-durch)", menu, checkable=True)
    act_lock.setChecked(hud.state["locked"])
    act_lock.triggered.connect(hud.set_locked)
    hud.hudLockedChanged.connect(act_lock.setChecked)
    menu.addAction(act_lock)

    # Die OBS-Schalter gibt es nur im QML-Renderer.
    if hasattr(hud, "set_chroma"):
        act_obs = QAction("OBS-Fenster anzeigen", menu, checkable=True)
        act_obs.setChecked(bool(hud.state.get("obs_window")))
        act_obs.setToolTip("Eigenes Fenster in Canvas-Groesse, das OBS aufnehmen kann.")
        act_obs.triggered.connect(
            lambda on: panel.set_obs_window(on) if panel else None)
        act_obs.setEnabled(panel is not None)
        menu.addAction(act_obs)

        act_find = QAction("OBS: HUD-Fenster auffindbar machen", menu, checkable=True)
        act_find.setChecked(bool(hud.state.get("obs_findable")))
        act_find.setToolTip("Ohne das listet OBS das HUD nicht auf (Qt.Tool).")
        act_find.triggered.connect(hud.set_findable)
        menu.addAction(act_find)

        act_chroma = QAction("OBS: HUD-Fenster einfarbig (Chroma-Key)", menu,
                             checkable=True)
        act_chroma.setChecked(bool(hud.state.get("obs_chroma")))
        act_chroma.triggered.connect(lambda on: hud.set_chroma(on))
        menu.addAction(act_chroma)

    menu.addSeparator()
    if panel is not None:
        menu.addAction(QAction("Bildschirm fuellen", menu, triggered=panel.fill_screen))
    menu.addAction(QAction("Neu laden", menu, triggered=hud.reload_page))
    menu.addSeparator()
    if panel is not None:
        menu.addAction(QAction("Alles beenden (mit Server)", menu,
                               triggered=panel.shutdown_all))
    menu.addAction(QAction("Nur HUD beenden", menu, triggered=app.quit))

    tray.setContextMenu(menu)
    # Linksklick aufs Icon = schnell sperren/entsperren, ohne Menue
    tray.activated.connect(
        lambda reason: hud.toggle_locked()
        if reason == QSystemTrayIcon.ActivationReason.Trigger else None)
    tray.show()
    # Kurze Rueckmeldung, dass ueberhaupt etwas laeuft. Der Starter benutzt pythonw
    # (kein Konsolenfenster), sonst sieht man vom Start naemlich gar nichts.
    tray.showMessage("KERS HUD laeuft",
                     "Rechtsklick auf das Tray-Icon oeffnet die Subsystems.",
                     icon, 4000)
    return tray


def main() -> int:
    _install_crash_log()
    parser = argparse.ArgumentParser(description="KERS Overlay als Always-on-Top-Fenster")
    parser.add_argument("--url", help="Basis-URL des Servers, z.B. http://127.0.0.1:5100")
    parser.add_argument("--page", help="Seite im HUD, z.B. / oder /test (nur --web)")
    parser.add_argument("--qml", dest="renderer", action="store_const", const="qml",
                        help="QML-Szene rendern (Vorgabe)")
    parser.add_argument("--web", dest="renderer", action="store_const", const="web",
                        help="die HTML-Seiten in einer WebEngineView rendern")
    parser.add_argument("--demo", nargs="?", const="race", choices=["race", "quali"],
                        help="erfundenes Rennen statt Server - zum Bauen am Overlay")
    parser.add_argument("--obs", action="store_true",
                        help="eigenes OBS-Fenster oeffnen (der zuverlaessige Weg)")
    parser.add_argument("--chroma", action="store_true",
                        help="HUD-Fenster einfarbig statt durchsichtig (Rueckfallebene)")
    parser.add_argument("--unlocked", action="store_true", help="entsperrt starten")
    parser.add_argument("--no-panel", action="store_true", help="ohne Steuerfenster starten")
    parser.add_argument("--no-gpu", action="store_true",
                        help="GPU-Compositing aus - hilft, wenn der Hintergrund schwarz bleibt")
    parser.add_argument("--hz", type=int, help="Overlay-Renderrate (10/20/30/60/120)")
    parser.add_argument("--devtools", action="store_true",
                        help="DevTools auf Port 9222 (nur --web)")
    args = parser.parse_args()

    # Muss VOR load_state() stehen: holt Zustand, Einstellungen, WM-Staende und die
    # entpackte main.exe aus dem alten Ort (neben der EXE) in den Datenordner.
    migrate_data()

    state = load_state()
    if args.url:
        state["base_url"] = args.url.rstrip("/")
    if args.page:
        state["page"] = args.page
    if args.renderer:
        state["renderer"] = args.renderer
    if args.unlocked:
        state["locked"] = False
    if args.hz:
        state["hz"] = int(args.hz)
    if args.chroma:
        state["obs_chroma"] = True
    qml = state.get("renderer", "qml") != "web"

    if qml:
        # Muss vor der QApplication laufen: Alphakanal anmelden und den PySide6-
        # Ordner in den DLL-Suchpfad legen (sonst laden die Effekt-Plugins nicht).
        from qml_overlay import prepare_qml_runtime
        prepare_qml_runtime()
    else:
        # Chromium-Flags MUESSEN vor dem QApplication-Konstruktor gesetzt sein.
        flags = os.environ.get("QTWEBENGINE_CHROMIUM_FLAGS", "")
        flags += " --enable-transparent-visuals"
        # Der Back-Forward-Cache haelt verlassene Seitenzustaende im Speicher. Ein
        # Overlay navigiert nie vor oder zurueck - hier kostet er nur.
        flags += " --disable-features=BackForwardCache"
        if args.no_gpu:
            flags += " --disable-gpu --disable-gpu-compositing"
        os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = flags.strip()

        if args.devtools:
            # Danach im Browser http://127.0.0.1:9222 oeffnen: Performance-Monitor
            # (JS-Heap, DOM-Knoten, Layer), Memory-Snapshot, Rendering -> Layer borders.
            os.environ["QTWEBENGINE_REMOTE_DEBUGGING"] = "9222"
            print("[HUD] DevTools aktiv -> http://127.0.0.1:9222")

    _claim_taskbar_identity()
    app = QApplication(sys.argv)
    app.setApplicationName("KERS HUD")
    app.setWindowIcon(make_icon())
    # Ohne das beendet sich alles, sobald HUD und Steuerung einmal zu sind.
    app.setQuitOnLastWindowClosed(False)

    if not QSystemTrayIcon.isSystemTrayAvailable():
        print("[HUD] Warnung: kein System-Tray gefunden.")

    api = ApiClient(state["base_url"])
    obs = None
    if qml:
        from qml_overlay import QmlOverlayWindow
        hud = QmlOverlayWindow(state, demo=args.demo or "")
        hud.start()                      # Datenstrom oeffnen (SSE bzw. Demo)
        if args.obs:
            state["obs_window"] = True
        if state.get("obs_window"):
            # Zweites Fenster fuer die Aufnahme. Teilt sich die Bruecke mit dem
            # HUD - ein Datenstrom, zweimal gezeichnet. Warum es das ueberhaupt
            # gibt, steht ausfuehrlich im Kopf von obs_window.py.
            from obs_window import ObsWindow
            obs = ObsWindow(state, hud.bridge)
            obs.show()
        hud.obs_window = obs             # das Schaltbrett spricht darueber mit ihm
    else:
        from overlay_window import OverlayWindow
        hud = OverlayWindow(state)

    panel = None
    if not args.no_panel:
        panel = SubsystemsPanel(state, hud, api)
        # Das Schaltbrett geht IMMER auf. Es war vorher an den gemerkten Zustand
        # gekoppelt - dadurch blieb es nach einmaligem Zumachen fuer immer weg und
        # man kam nur noch ueber das Tray-Icon heran (oder gar nicht, wenn auch das
        # HUD aus war). Zumachen waehrend der Sitzung blendet weiterhin nur aus.
        panel.show()
        state["panel_visible"] = True

    tray = build_tray(app, hud, panel)  # noqa: F841 - Referenz halten
    api.start()

    def _shutdown():
        api.stop()
        stop = getattr(hud, "stop", None)     # QML: den SSE-Thread sauber beenden
        if stop is not None:
            stop()
        if panel is not None:
            state["panel_visible"] = panel.isVisible()
        save_state(state)

    app.aboutToQuit.connect(_shutdown)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
