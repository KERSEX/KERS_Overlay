"""
Das QML-HUD-Fenster - der Ersatz fuer overlay_window.py (QtWebEngine).

Die Fenster-Technik ist dieselbe geblieben, sie ist unabhaengig vom Inhalt:

    Qt.FramelessWindowHint        kein Rahmen, keine Titelleiste
    Qt.WindowStaysOnTopHint       bleibt ueber allen anderen Fenstern
    Qt.Tool                       kein Taskbar- und kein Alt-Tab-Eintrag
    Qt.WindowTransparentForInput  Klicks gehen durch (nur im gesperrten Zustand)

Was sich geaendert hat:

    * Statt einer QWebEngineView haengt hier eine QQuickView. Kein Chromium mehr,
      kein zweiter Prozess, keine 80 MB Runtime - die Szene rendert direkt ueber
      die Grafikkarte.
    * Die EditGlass-Scheibe aus der alten Datei entfaellt. Sie war noetig, weil die
      WebEngineView alle Mausereignisse schluckte und man das Fenster sonst nicht
      mehr greifen konnte. In QML ist der Bearbeiten-Rahmen einfach ein Element in
      der Szene (EditFrame.qml); die Geometrie-Rechnerei dazu steht hier unten in
      HudController und ist 1:1 die aus dem alten EditGlass._apply_resize.
    * Transparenz kommt nicht mehr aus einem CSS-Body, sondern aus dem Alphakanal
      des Fensters (setColor(transparent) + Alphapuffer im Surface-Format).

Die Signale nach aussen heissen absichtlich genauso wie bei OverlayWindow
(hudGeometryChanged, hudLockedChanged, hudVisibilityChanged) - das Schaltbrett und
das Tray-Menue kommen damit ohne Sonderfaelle aus, egal welcher Renderer laeuft.
"""

import ctypes
import os
import sys
from datetime import datetime
from pathlib import Path

import PySide6
from PySide6.QtCore import Property, QObject, QTimer, QUrl, Qt, Signal, Slot
from PySide6.QtGui import QColor, QSurfaceFormat
from PySide6.QtQuick import QQuickView

from bridge import OverlayBridge
from common import DATA_DIR
from theme import Theme, load_fonts

# Als EXE liegt das gebuendelte qml/ unter sys._MEIPASS/hud/qml, nicht relativ
# zu __file__ (das zeigt in den Pyinstaller-Temp-Ordner).
if getattr(sys, "frozen", False):
    QML_DIR = Path(sys._MEIPASS) / "hud" / "qml"
else:
    QML_DIR = Path(__file__).resolve().parent / "qml"

MIN_W, MIN_H = 240, 160


def _ist_weggeblendet(hwnd) -> bool:
    """DWM-Cloaking: Fenster gilt als sichtbar, ist aber weggeblendet."""
    wert = ctypes.c_int(0)
    try:
        ok = ctypes.windll.dwmapi.DwmGetWindowAttribute(
            ctypes.c_void_p(hwnd), DWMWA_CLOAKED,
            ctypes.byref(wert), ctypes.sizeof(wert))
    except Exception:  # pylint: disable=broad-exception-caught
        return False                          # dwmapi fehlt: dann eben ungefiltert
    return ok == 0 and wert.value != 0


def _fensterklasse(hwnd) -> str:
    puffer = ctypes.create_unicode_buffer(128)
    ctypes.windll.user32.GetClassNameW(ctypes.c_void_p(hwnd), puffer, 128)
    return puffer.value


def _fenster_beschreiben(hwnd) -> str:
    """Klasse, Titel und Prozess-ID - genug, um den Stoerenfried zu erkennen."""
    user32 = ctypes.windll.user32
    klasse = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(ctypes.c_void_p(hwnd), klasse, 128)
    titel = ctypes.create_unicode_buffer(160)
    user32.GetWindowTextW(ctypes.c_void_p(hwnd), titel, 160)
    pid = ctypes.c_ulong(0)
    user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(pid))
    eigen = " [wir selbst]" if pid.value == os.getpid() else ""
    return f"{klasse.value!r} \"{titel.value}\" (PID {pid.value}){eigen}"

# Win32-Konstanten fuer den Auffindbar-Schalter (siehe _apply_native_toolwindow).
GWL_EXSTYLE = -20
WS_EX_TOOLWINDOW = 0x00000080
SW_HIDE = 0
SW_SHOWNOACTIVATE = 4

# ... und fuer das Nachfassen der Vordergrund-Lage (siehe _reassert_topmost).
HWND_TOPMOST = -1
WS_EX_TOPMOST = 0x00000008
SWP_NOSIZE = 0x0001
SWP_NOMOVE = 0x0002
SWP_NOACTIVATE = 0x0010
SWP_NOOWNERZORDER = 0x0200
GW_HWNDPREV = 3
DWMWA_CLOAKED = 14
# Wie oft nachgefasst wird. 2 s ist ein Kompromiss: haeufig genug, dass ein
# Wegrutschen kaum auffaellt, selten genug, dass es nichts kostet.
TOPMOST_INTERVAL_MS = 2000
# Die Taskleiste ist selbst ein Vordergrundfenster und liegt beim bildschirm-
# fuellenden HUD staendig darueber - sie waere die haeufigste Meldung im Log und
# wuerde den echten Fund zudecken. Nachgemessen mit GetClassNameW: der Balken
# heisst Shell_TrayWnd, auf den weiteren Bildschirmen Shell_SecondaryTrayWnd.
DIAGNOSE_EGAL = ("Shell_TrayWnd", "Shell_SecondaryTrayWnd")
# Wie weit die Diagnose die Fensterreihenfolge hochlaeuft, bevor sie aufgibt.
# Ueber einem Vordergrundfenster liegen normalerweise 0-2 Fenster; die Grenze
# ist nur da, damit der Timer unter keinen Umstaenden haengen bleibt.
MAX_ZORDER_WALK = 60
# Wohin die Diagnose schreibt. Im Dev-Betrieb neben hud/, als EXE in data/ -
# denn die EXE hat ohne "build.bat -c" gar keine Konsole, in der ein print
# landen koennte.
TOPMOST_LOG = DATA_DIR / "hud_topmost.log"

# Unter diesen Namen sieht die QML-Szene die drei Python-Objekte:
#     Kers    der Rennstand (bridge.OverlayBridge)
#     Theme   Farben, Masse, Asset-Pfade (theme.Theme)
#     Hud     der Fensterzustand (HudController weiter unten)
#
# ⚠ BEWUSST als Context-Properties und NICHT als QML-Singletons.
# qmlRegisterSingletonInstance() ist in PySide6 6.11 unter Windows kaputt: sobald
# EIN Singleton registriert ist, verliert die Engine die Typidentitaet der aus
# Plugin-DLLs geladenen Module. QtQuick.Shapes und Qt5Compat.GraphicalEffects
# scheitern dann mit
#     Cannot assign object of type "QQuickShapePath" to list property "data";
#     expected "QObject"
# obwohl dieselbe QML-Datei ohne die Registrierung einwandfrei laedt (nachgestellt
# mit einem leeren QObject - es liegt an der Registrierung selbst, nicht am Objekt).
# Context-Properties haben denselben Effekt fuer uns und sind davon nicht betroffen.
CONTEXT_NAMES = ("Kers", "Theme", "Hud")


def prepare_qml_runtime() -> None:
    """Muss VOR der QApplication laufen. Erledigt zwei Dinge:

    1) ALPHAKANAL anmelden. Ohne das bekommt die Szene einen undurchsichtigen
       Puffer und der Hintergrund bleibt schwarz statt durchsichtig.

    2) DEN PYSIDE6-ORDNER IN DEN DLL-SUCHPFAD legen.
       ⚠ Ohne diesen Schritt laesst sich QtQuick.Effects (MultiEffect) und
       Qt5Compat.GraphicalEffects (ConicalGradient) nicht laden - das Overlay
       zeigt dann gar nichts und meldet nur "Cannot load library
       effectsplugin.dll: Das angegebene Modul wurde nicht gefunden."

       Grund: Python setzt unter Windows SetDefaultDllDirectories(), damit sucht
       Windows Abhaengigkeiten nur noch im Programmordner, in System32 und in
       ausdruecklich angemeldeten Ordnern - PATH zaehlt nicht mehr. Die QML-Plugins
       liegen aber in PySide6/qml/... und brauchen Qt6QuickEffects.dll aus
       PySide6/ selbst. Qt6Quick.dll & Co. faellt das nicht auf, die sind zu dem
       Zeitpunkt laengst geladen; Qt6QuickEffects.dll ist es nicht.
    """
    try:
        os.add_dll_directory(str(Path(PySide6.__file__).resolve().parent))
    except (AttributeError, OSError) as e:
        print(f"[HUD] DLL-Suchpfad nicht erweiterbar: {e}")

    fmt = QSurfaceFormat.defaultFormat()
    fmt.setAlphaBufferSize(8)
    QSurfaceFormat.setDefaultFormat(fmt)


class HudController(QObject):
    """Der Fensterzustand, wie QML ihn sieht: gesperrt, Bearbeiten, OBS-Modus.

    Liegt bewusst neben dem Fenster und nicht darin: QML soll ein schlichtes
    Objekt mit Properties sehen und nichts von QQuickView wissen muessen.
    """

    lockedChanged = Signal(bool)
    layoutEditChanged = Signal(bool)
    bgChanged = Signal()

    # Hintergrundfarbe im Chroma-Key-Modus. Magenta, weil es in einem Formel-1-
    # Overlay garantiert nicht vorkommt - Gruen kollidiert mit Sektor-Gruen,
    # Blau mit den Teamfarben von Alpine/Williams/RB.
    DEFAULT_CHROMA = "#FF00FF"

    def __init__(self, window: "QmlOverlayWindow", state: dict, parent=None):
        super().__init__(parent)
        self._window = window
        self._state = state
        self._locked = bool(state.get("locked", True))
        self._chroma = bool(state.get("obs_chroma", False))
        self._chroma_color = str(state.get("obs_chroma_color") or self.DEFAULT_CHROMA)
        self._findable = bool(state.get("obs_findable", False))
        # Laufender Bearbeiten-Vorgang: Ausgangsgeometrie + Ausgangsposition der Maus
        self._edit_corner = ""
        self._origin_geo = None
        self._origin_mouse = None
        # Layout-Bearbeiten (Bausteine verschieben) und der Sperrzustand davor
        self._layout_edit = False
        self._locked_vorher = True

    # ── Gesperrt / Bearbeiten ────────────────────────────────────────────────
    @Property(bool, notify=lockedChanged)
    def locked(self):
        return self._locked

    @locked.setter
    def locked(self, value):
        self._window.set_locked(bool(value))

    def _set_locked_silent(self, value: bool) -> None:
        """Vom Fenster gerufen, nachdem es die Flags gesetzt hat."""
        if self._locked != value:
            self._locked = value
            self.lockedChanged.emit(value)

    # ── Layout bearbeiten ────────────────────────────────────────────────────
    @Property(bool, notify=layoutEditChanged)
    def layoutEdit(self):
        """True = Bausteine lassen sich mit der Maus verschieben."""
        return self._layout_edit

    @layoutEdit.setter
    def layoutEdit(self, value):
        self.set_layout_edit(bool(value))

    def set_layout_edit(self, on: bool) -> None:
        """Bearbeiten an/aus. Schaltet das Fenster gleich mit frei.

        ⚠ Gesperrt gehen alle Klicks durch das Fenster hindurch
        (WindowTransparentForInput) - die Szene bekaeme gar keine Maus. Zum
        Bearbeiten muss also entsperrt werden. Beim Ausschalten wird der
        vorherige Zustand wiederhergestellt, damit das Overlay nicht
        versehentlich entsperrt im Rennen steht und Klicks abfaengt.
        """
        on = bool(on)
        if on == self._layout_edit:
            return
        if on:
            self._locked_vorher = self._window.state["locked"]
            if self._locked_vorher:
                self._window.set_locked(False)
            # Fokus holen, damit Esc ankommt. Im Alltag wird das Fenster bewusst
            # ohne Fokus gezeigt (SW_SHOWNOACTIVATE, siehe
            # _apply_native_toolwindow) - beim Bearbeiten arbeitet man aber
            # ohnehin genau hier, und ohne aktives Fenster kaeme keine Taste an.
            self._window.requestActivate()
        self._layout_edit = on
        # Erfundene Daten einspeisen: ohne laufendes Rennen waere die Szene leer
        # und es gaebe nichts zum Verschieben.
        self._window.bridge.set_vorschau(on)
        self.layoutEditChanged.emit(on)
        if not on and self._locked_vorher:
            self._window.set_locked(True)
        # Nach draussen melden: der Fertig-Knopf sitzt auch IN der Szene (das
        # Schaltbrett liegt beim Bearbeiten unter der entsperrten Klickflaeche),
        # also muss sein Gegenstueck im Schaltbrett hinterherziehen.
        self._window.hudLayoutEditChanged.emit(on)

    # ── Hintergrund / OBS ────────────────────────────────────────────────────
    @Property(bool, notify=bgChanged)
    def chroma(self):
        """True = einfarbiger Hintergrund statt Alphakanal (Rueckfallebene fuer OBS)."""
        return self._chroma

    @chroma.setter
    def chroma(self, value):
        self._window.set_chroma(bool(value))

    @Property(QColor, notify=bgChanged)
    def chromaColor(self):
        return QColor(self._chroma_color)

    def _set_bg_silent(self, chroma: bool, color: str) -> None:
        if (self._chroma, self._chroma_color) == (chroma, color):
            return
        self._chroma = chroma
        self._chroma_color = color
        self.bgChanged.emit()

    @Property(bool, notify=bgChanged)
    def findable(self):
        """True = das Fenster taucht in der Fensterliste von OBS auf."""
        return self._findable

    @findable.setter
    def findable(self, value):
        self._window.set_findable(bool(value))

    def _set_findable_silent(self, value: bool) -> None:
        if self._findable != value:
            self._findable = value
            self.bgChanged.emit()

    # ── Bearbeiten-Modus: Fenster ziehen und skalieren ───────────────────────
    # Die Rechnerei ist die aus EditGlass._apply_resize in overlay_window.py: die
    # gegenueberliegende Ecke bleibt stehen, und unter MIN_W/MIN_H geht es nicht.
    @Slot(str, int, int)
    def beginEdit(self, corner: str, global_x: int, global_y: int) -> None:
        self._edit_corner = corner or ""
        self._origin_geo = self._window.geometry()
        self._origin_mouse = (global_x, global_y)

    @Slot(int, int)
    def editTo(self, global_x: int, global_y: int) -> None:
        if self._origin_geo is None:
            return
        dx = global_x - self._origin_mouse[0]
        dy = global_y - self._origin_mouse[1]
        geo = self._origin_geo

        if not self._edit_corner:                      # ziehen = verschieben
            self._window.setPosition(geo.x() + dx, geo.y() + dy)
            return

        x, y, w, h = geo.x(), geo.y(), geo.width(), geo.height()
        if "l" in self._edit_corner:
            x, w = geo.x() + dx, geo.width() - dx
        else:
            w = geo.width() + dx
        if "t" in self._edit_corner:
            y, h = geo.y() + dy, geo.height() - dy
        else:
            h = geo.height() + dy

        if w < MIN_W:
            w = MIN_W
            if "l" in self._edit_corner:
                x = geo.x() + geo.width() - MIN_W
        if h < MIN_H:
            h = MIN_H
            if "t" in self._edit_corner:
                y = geo.y() + geo.height() - MIN_H
        self._window.setGeometry(x, y, w, h)

    @Slot()
    def endEdit(self) -> None:
        self._edit_corner = ""
        self._origin_geo = None
        self._origin_mouse = None
        self._window.remember_geometry()


class QmlOverlayWindow(QQuickView):
    """Rahmenloses Always-on-Top-Fenster mit der QML-Overlay-Szene darin."""

    # Gleiche Namen wie in overlay_window.py - siehe Modulkommentar.
    hudGeometryChanged = Signal(int, int, int, int)
    hudLockedChanged = Signal(bool)
    hudVisibilityChanged = Signal(bool)
    sceneLoaded = Signal(bool)
    # Nur hier, nicht im Web-Renderer: der kennt kein Layout-Bearbeiten. Das
    # Schaltbrett verbindet sich deshalb nur, wenn es die Methode dazu findet.
    hudLayoutEditChanged = Signal(bool)

    def __init__(self, state: dict, demo: str = ""):
        super().__init__()
        self.state = state
        self._want_visible = bool(state["visible"])

        load_fonts()

        self.setTitle("KERS HUD")
        self.setFlags(Qt.WindowType.FramelessWindowHint
                      | Qt.WindowType.WindowStaysOnTopHint
                      | Qt.WindowType.Tool)
        self.setColor(QColor(0, 0, 0, 0))
        self.setResizeMode(QQuickView.ResizeMode.SizeRootObjectToView)

        # Die drei Objekte, die die Szene sieht. Muessen vor setSource() stehen.
        self.theme = Theme(self)
        self.bridge = OverlayBridge(state["base_url"], int(state.get("hz", 30)),
                                    demo=demo, parent=self)
        self.hud = HudController(self, state, self)
        # Branding und Deckkraft wandern aus den Settings ins Theme - das ist das
        # Gegenstueck zu applyBrand() in core.js, das dieselben Werte in :root schreibt.
        self.bridge.settings.brandAccentChanged.connect(self._sync_theme)
        self.bridge.settings.headerColorChanged.connect(self._sync_theme)
        self.bridge.settings.rowColorChanged.connect(self._sync_theme)
        self.bridge.settings.uiAlphaChanged.connect(self._sync_theme)
        self._sync_theme()

        ctx = self.engine().rootContext()
        ctx.setContextProperty("Theme", self.theme)
        ctx.setContextProperty("Kers", self.bridge)
        ctx.setContextProperty("Hud", self.hud)

        self.statusChanged.connect(self._on_status)

        # Faellt das Fenster in der Vordergrund-Schicht zurueck, holt der Timer es
        # wieder nach vorn. Begruendung steht bei _reassert_topmost.
        self._topmost_timer = QTimer(self)
        self._topmost_timer.setInterval(TOPMOST_INTERVAL_MS)
        self._topmost_timer.timeout.connect(self._reassert_topmost)
        # Letzter Diagnose-Befund, damit nur ZUSTANDSWECHSEL im Log landen.
        self._letzter_befund = ""

        self.setGeometry(state["x"], state["y"], state["w"], state["h"])
        self.setOpacity(state["opacity"] / 100.0)
        self.set_chroma(bool(state.get("obs_chroma", False)),
                        str(state.get("obs_chroma_color") or HudController.DEFAULT_CHROMA))
        self.load_scene()
        self.update_window_flags()

    # ── Szene ────────────────────────────────────────────────────────────────
    def load_scene(self) -> None:
        self.setSource(QUrl.fromLocalFile(str(QML_DIR / "Overlay.qml")))

    def reload_page(self) -> None:
        """Szene neu laden - das Gegenstueck zu 'Neu laden' im Tray-Menue.

        clearComponentCache() ist der eigentliche Zweck: ohne das laedt die Engine
        die alten, bereits uebersetzten QML-Dateien wieder aus dem Cache und
        Aenderungen am QML werden nicht sichtbar.
        """
        self.engine().clearComponentCache()
        self.load_scene()

    def _on_status(self, status) -> None:
        if status == QQuickView.Status.Error:
            for err in self.errors():
                print(f"[HUD] QML-Fehler: {err.toString()}")
            self.sceneLoaded.emit(False)
        elif status == QQuickView.Status.Ready:
            self.sceneLoaded.emit(True)

    def _sync_theme(self) -> None:
        self.theme.apply_settings(self.bridge.settings)

    # ── Datenstrom ───────────────────────────────────────────────────────────
    def start(self) -> None:
        self.bridge.start()

    def stop(self) -> None:
        self.bridge.stop()

    def set_base_url(self, base: str) -> None:
        self.state["base_url"] = base.rstrip("/")
        self.bridge.set_base_url(self.state["base_url"])

    def set_hz(self, hz: int) -> None:
        self.state["hz"] = int(hz)
        self.bridge.set_hz(int(hz))

    def set_page(self, path: str) -> None:
        """Gibt es im QML-Renderer (noch) nicht.

        Die Bausteine sind hier keine eigenen Seiten, sondern Elemente EINER Szene -
        welche davon sichtbar sind, entscheiden die Settings. Die Methode bleibt
        stehen, damit das Schaltbrett nicht zwei Faelle kennen muss.
        """
        self.state["page"] = path

    # ── Fensterflags ─────────────────────────────────────────────────────────
    def update_window_flags(self) -> None:
        flags = (Qt.WindowType.FramelessWindowHint
                 | Qt.WindowType.WindowStaysOnTopHint)

        # ⚠ Qt.Tool ist der Grund, warum OBS das HUD nicht findet.
        # Qt setzt dafuer WS_EX_TOOLWINDOW, und OBS wirft jedes Fenster mit diesem
        # Stil aus seiner Liste (win-capture/window-helpers.c, check_window_valid).
        # Der Stil ist gleichzeitig das, was den Taskleisten- und Alt-Tab-Eintrag
        # unterdrueckt - beides haengt an derselben Eigenschaft, man kann es nur
        # zusammen haben. Deshalb der Schalter: im Alltag aus (nichts in der
        # Taskleiste), zum Aufnehmen an (dafuer taucht es in OBS auf).
        if not self.state.get("obs_findable"):
            flags |= Qt.WindowType.Tool
        if self.state["locked"]:
            flags |= Qt.WindowType.WindowTransparentForInput

        geo = self.geometry()
        self.setFlags(flags)      # macht das Fenster unter Windows unsichtbar
        self.setGeometry(geo)     # ... und vergisst dabei gerne die Geometrie
        if self._want_visible:
            self.show()
        self._apply_native_toolwindow()
        # setFlags() baut das native Fenster neu auf - danach steht es zwar wieder
        # in der Vordergrund-Schicht, aber nicht zwingend vorn. Deshalb hier gleich
        # nachfassen, statt bis zum naechsten Timer-Schlag zu warten.
        self._reassert_topmost()
        self._sync_topmost_timer()

    def _apply_native_toolwindow(self) -> None:
        """WS_EX_TOOLWINDOW am NATIVEN Fenster setzen bzw. wegnehmen.

        ⚠ Warum das nicht ueber setFlags() geht: Qt wertet Qt.Tool nur aus, wenn es
        das native Fenster ERZEUGT. Ein spaeteres setFlags() laesst den erweiterten
        Fensterstil unveraendert - nachgemessen mit GetWindowLongPtrW: der Wert
        blieb bei 0x000800A8, egal was Qt gesagt bekam. Der Schalter haette also
        schlicht nichts getan.

        Deshalb hier direkt ueber die Win32-API. Zum Umsetzen muss das Fenster kurz
        verschwinden - Windows uebernimmt die Aenderung an diesem Stil sonst erst
        beim naechsten Neuaufbau.
        """
        if sys.platform != "win32":
            return
        try:
            user32 = ctypes.windll.user32
            # Auf 64 Bit heisst die Funktion ...LongPtrW, auf 32 Bit nur ...LongW.
            get = getattr(user32, "GetWindowLongPtrW", None) or user32.GetWindowLongW
            put = getattr(user32, "SetWindowLongPtrW", None) or user32.SetWindowLongW
            get.restype = ctypes.c_ssize_t
            get.argtypes = [ctypes.c_void_p, ctypes.c_int]
            put.restype = ctypes.c_ssize_t
            put.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_ssize_t]

            hwnd = ctypes.c_void_p(int(self.winId()))
            current = get(hwnd, GWL_EXSTYLE)
            want_tool = not self.state.get("obs_findable")
            target = (current | WS_EX_TOOLWINDOW) if want_tool \
                else (current & ~WS_EX_TOOLWINDOW)
            if target == current:
                return

            visible = self.isVisible()
            if visible:
                user32.ShowWindow(hwnd, SW_HIDE)
            put(hwnd, GWL_EXSTYLE, target)
            if visible:
                # NOACTIVATE: das Overlay soll dem Spiel nicht den Fokus klauen.
                user32.ShowWindow(hwnd, SW_SHOWNOACTIVATE)
        except Exception as e:  # pylint: disable=broad-exception-caught
            print(f"[HUD] Fensterstil nicht aenderbar: {e}")

    # ── Vordergrund halten ───────────────────────────────────────────────────
    def _reassert_topmost(self) -> None:
        """Das Fenster wieder an die Spitze der Vordergrund-Schicht setzen.

        ⚠ Warum das noetig ist, obwohl WindowStaysOnTopHint gesetzt ist:
        WS_EX_TOPMOST sagt unter Windows nur, dass das Fenster in die OBERE
        Schicht gehoert - nicht, dass es dort das oberste ist. Innerhalb dieser
        Schicht gilt weiter die normale Reihenfolge, und jedes Fenster, das sich
        SPAETER als Vordergrundfenster anmeldet, legt sich davor: Spiel-Overlays,
        Discord, OBS-Projektor, Windows-Benachrichtigungen. Das HUD ist dann
        immer noch "always on top" und trotzdem verdeckt - genau das Bild, das
        der Fehler macht.

        SetWindowPos(HWND_TOPMOST) meldet uns erneut oben an. NOACTIVATE ist
        dabei Pflicht: ohne das nimmt das Overlay dem Spiel den Fokus.
        NOOWNERZORDER laesst das Besitzerfenster in Ruhe - Qt.Tool haengt das
        HUD an ein solches, und ohne das Flag wuerde es mitwandern.

        Was das NICHT heilt: ein Spiel im exklusiven Vollbild. Dort gibt es
        ueberhaupt keine Fensterschicht mehr - dafuer muss F1 auf randloses
        Fenster bzw. Vollbild-Fenster stehen.
        """
        if sys.platform != "win32" or not self.isVisible():
            return
        try:
            hwnd = ctypes.c_void_p(int(self.winId()))
            self._diagnose_lage(hwnd)
            ctypes.windll.user32.SetWindowPos(
                hwnd, ctypes.c_void_p(HWND_TOPMOST), 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)
        except Exception as e:  # pylint: disable=broad-exception-caught
            # Einmal melden reicht - der Timer liefe sonst alle 2 s in die Ausgabe.
            print(f"[HUD] Vordergrund nicht durchsetzbar: {e}")
            self._topmost_timer.stop()

    # ── Diagnose: WER legt sich davor? ───────────────────────────────────────
    # Der Fehler tritt selten und ohne erkennbaren Ausloeser auf. Statt zu raten
    # schreibt das HUD deshalb mit, sobald es sich verdeckt vorfindet - und zwar
    # NUR bei einer Aenderung, sonst stuende alle 2 s dieselbe Zeile im Log.
    def _diagnose_lage(self, hwnd) -> None:
        user32 = ctypes.windll.user32
        get = getattr(user32, "GetWindowLongPtrW", None) or user32.GetWindowLongW
        get.restype = ctypes.c_ssize_t
        get.argtypes = [ctypes.c_void_p, ctypes.c_int]

        # Zwei verschiedene Fehlerbilder, die man auseinanderhalten muss:
        #   Flag weg   -> das Fenster gehoert gar nicht mehr in die obere Schicht
        #   Flag da    -> es gehoert hinein, ist dort aber nicht das oberste
        hat_flag = bool(get(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
        darueber = self._fenster_ueber_uns(hwnd)

        if hat_flag and not darueber:
            befund = ""                      # alles in Ordnung
        elif not hat_flag:
            befund = f"WS_EX_TOPMOST verloren; darueber: {darueber or 'nichts'}"
        else:
            befund = f"verdeckt von {darueber}"

        if befund == self._letzter_befund:
            return
        self._letzter_befund = befund
        if not befund:
            return
        zeile = f"{datetime.now():%Y-%m-%d %H:%M:%S}  {befund}"
        print(f"[HUD] {zeile}")
        try:
            with open(TOPMOST_LOG, "a", encoding="utf-8") as f:
                f.write(zeile + "\n")
        except OSError:
            pass                             # Diagnose darf den Betrieb nie stoeren

    def _fenster_ueber_uns(self, hwnd) -> str:
        """Das naechste SICHTBARE Fenster ueber uns - leerer String: keins.

        ⚠ IsWindowVisible allein reicht nicht. Store-/UWP-Anwendungen lassen
        staendig Fenster stehen, die als sichtbar gelten, von der DWM aber
        "cloaked" (weggeblendet) sind. Ohne diese zweite Pruefung meldet die
        Diagnose bei praktisch jedem Durchgang einen Stoerenfried, den man auf
        dem Bildschirm gar nicht sieht - und waere damit wertlos.
        """
        user32 = ctypes.windll.user32
        cur = user32.GetWindow(hwnd, GW_HWNDPREV)
        for _ in range(MAX_ZORDER_WALK):
            if not cur:
                return ""
            if (user32.IsWindowVisible(cur)
                    and not _ist_weggeblendet(cur)
                    and _fensterklasse(cur) not in DIAGNOSE_EGAL):
                return _fenster_beschreiben(cur)
            cur = user32.GetWindow(cur, GW_HWNDPREV)
        return "(Reihenfolge zu lang - abgebrochen)"

    def _sync_topmost_timer(self) -> None:
        """Nur mitlaufen lassen, solange das HUD ueberhaupt zu sehen ist."""
        if sys.platform != "win32":
            return
        if self.isVisible():
            if not self._topmost_timer.isActive():
                self._topmost_timer.start()
        else:
            self._topmost_timer.stop()

    def set_layout_edit(self, on: bool) -> None:
        """Vom Schaltbrett: Bausteine verschiebbar machen. Nur im QML-Renderer."""
        self.hud.set_layout_edit(bool(on))

    def layout_zuruecksetzen(self) -> None:
        """Vom Schaltbrett: alle Bausteine zurueck an ihren Platz.

        Ein leeres Layout geht denselben Weg wie eine Verschiebung: erst lokal
        uebernehmen, dann an den Server, und in der Antwort die lokale
        Ueberschreibung aufgeben. Kommt die Antwort nicht (kein Server), bleibt
        die Ueberschreibung stehen - zurueckgesetzt ist es dann bis zum
        Neustart. Begruendung ausfuehrlich in bridge.layoutSpeichern.
        """
        self.bridge.layoutSpeichern({})

    def set_findable(self, on: bool) -> None:
        """Fuer OBS auffindbar machen (kostet einen Taskleisten-Eintrag)."""
        self.state["obs_findable"] = bool(on)
        self._apply_native_toolwindow()
        # Der Stilwechsel blendet das Fenster kurz aus und wieder ein - dabei kann
        # es in der Vordergrund-Schicht nach hinten rutschen.
        self._reassert_topmost()
        self.hud._set_findable_silent(bool(on))

    def set_locked(self, locked: bool) -> None:
        self.state["locked"] = bool(locked)
        self.update_window_flags()
        self.hud._set_locked_silent(self.state["locked"])
        self.hudLockedChanged.emit(self.state["locked"])

    def toggle_locked(self) -> None:
        self.set_locked(not self.state["locked"])

    def set_hud_visible(self, visible: bool) -> None:
        self._want_visible = bool(visible)
        self.state["visible"] = self._want_visible
        self.setVisible(self._want_visible)
        # Beim Wiedereinblenden landet das Fenster nicht zwangslaeufig vorn.
        self._reassert_topmost()
        self._sync_topmost_timer()
        self.hudVisibilityChanged.emit(self._want_visible)

    # ── OBS-Hintergrund ──────────────────────────────────────────────────────
    def set_chroma(self, on: bool, color: str | None = None) -> None:
        """Zwischen Alphakanal und einfarbigem Hintergrund umschalten.

        Warum es beides gibt, steht ausfuehrlich in hud/README.md: OBS reicht den
        Alphakanal eines Fensters nicht bei jeder Aufnahmemethode durch. Klappt es,
        ist der transparente Modus klar besser; klappt es nicht, ist der einfarbige
        Hintergrund plus Farb-Key in OBS die Rueckfallebene, die immer geht.
        """
        color = color or self._chroma_color_or_default()
        self.state["obs_chroma"] = bool(on)
        self.state["obs_chroma_color"] = color
        self.setColor(QColor(color) if on else QColor(0, 0, 0, 0))
        # Im Chroma-Modus muessen die Panels deckend werden - sonst scheint die
        # Schluesselfarbe durch und der Farb-Key in OBS frisst sie anteilig mit.
        self.theme.set_opaque(bool(on))
        self.hud._set_bg_silent(bool(on), color)

    def _chroma_color_or_default(self) -> str:
        return str(self.state.get("obs_chroma_color") or HudController.DEFAULT_CHROMA)

    # ── Geometrie ────────────────────────────────────────────────────────────
    def apply_geometry(self, x: int, y: int, w: int, h: int) -> None:
        self.setGeometry(int(x), int(y), max(MIN_W, int(w)), max(MIN_H, int(h)))
        self.remember_geometry()

    def remember_geometry(self) -> None:
        geo = self.geometry()
        self.state.update({"x": geo.x(), "y": geo.y(),
                           "w": geo.width(), "h": geo.height()})
        self.hudGeometryChanged.emit(geo.x(), geo.y(), geo.width(), geo.height())

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.remember_geometry()

    def moveEvent(self, event):
        super().moveEvent(event)
        self.remember_geometry()
