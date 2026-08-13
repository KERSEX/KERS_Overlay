"""
Das OBS-Fenster: dieselbe Overlay-Szene noch einmal, aber aufnahmetauglich.

WARUM ES DAS BRAUCHT
--------------------
Die Fensteraufnahme von OBS zeigt vom HUD-Fenster nur EIN Bild und friert dann
ein. Das ist kein Fehler in OBS und keiner im Overlay, sondern eine Folge davon,
WIE ein durchsichtiges Fenster unter Windows zustande kommt:

    Qt setzt fuer ein Fenster mit Alphakanal WS_EX_LAYERED und laesst den Inhalt
    ueber DirectComposition unmittelbar in den Desktop zeichnen. Die Windows
    Graphics Capture, aus der OBS seine Fensteraufnahme speist, liest dagegen die
    DWM-Umleitungsflaeche des Fensters - und die bekommt bei so einem Fenster
    keine neuen Bilder mehr. OBS haelt deshalb das zuletzt gesehene Bild fest.

An EINEM Fenster laesst sich das nicht loesen: entweder es ist durchsichtig (dann
taugt es fuers Overlay auf dem Desktop, aber nicht fuer die Fensteraufnahme), oder
es ist undurchsichtig (dann umgekehrt). Also gibt es beides:

    HUD-Fenster    rahmenlos, durchsichtig, immer oben, Klicks gehen durch.
                   Fuers Auge auf dem zweiten Monitor bzw. beim Fahren.
    OBS-Fenster    ein ganz normales Fenster in Canvas-Groesse, undurchsichtig,
                   mit Schluesselfarbe als Hintergrund. Das nimmt OBS zuverlaessig
                   auf; die Schluesselfarbe holt ein Farbschluessel-Filter wieder raus.

Beide zeigen DIESELBE Szene und haengen an DERSELBEN Bruecke - es laeuft also nur
ein Datenstrom und eine Buchhaltung, nur eben zweimal gezeichnet. Wer das HUD auf
dem Desktop gar nicht braucht, schaltet es aus und laesst nur dieses Fenster laufen.

⚠ Eigenes Theme, nicht das des HUD-Fensters: im OBS-Fenster muessen alle Panels
deckend sein (sonst frisst der Farbschluessel sie anteilig mit weg), auf dem
Desktop sollen sie durchsichtig bleiben. Ein gemeinsames Theme koennte nur eines
von beidem.
"""

import ctypes
import sys
from pathlib import Path

from PySide6.QtCore import Property, QObject, QUrl, Qt, Signal, Slot
from PySide6.QtGui import QColor, QSurfaceFormat
from PySide6.QtQuick import QQuickView

from theme import Theme

# Als EXE liegt das gebuendelte qml/ unter sys._MEIPASS/hud/qml, nicht relativ
# zu __file__ (das zeigt in den Pyinstaller-Temp-Ordner).
if getattr(sys, "frozen", False):
    QML_DIR = Path(sys._MEIPASS) / "hud" / "qml"
else:
    QML_DIR = Path(__file__).resolve().parent / "qml"

DEFAULT_W, DEFAULT_H = 1920, 1080


class ObsController(QObject):
    """Was die Szene als `Hud` sieht, wenn sie im OBS-Fenster laeuft.

    Absichtlich abgespeckt: hier gibt es keinen Bearbeiten-Modus (die Groesse
    stellt man im Schaltbrett ein, nicht mit der Maus), und der Hintergrund ist
    immer die Schluesselfarbe. `locked` ist fest wahr, damit der Bearbeiten-Rahmen
    aus Overlay.qml gar nicht erst erscheint.
    """

    lockedChanged = Signal(bool)
    bgChanged = Signal()

    def __init__(self, color: str, parent=None):
        super().__init__(parent)
        self._color = color

    @Property(bool, notify=lockedChanged)
    def locked(self):
        return True

    @Property(bool, notify=bgChanged)
    def chroma(self):
        return True

    @Property(QColor, notify=bgChanged)
    def chromaColor(self):
        return QColor(self._color)

    @Property(bool, notify=bgChanged)
    def findable(self):
        return True

    def set_color(self, color: str) -> None:
        if color != self._color:
            self._color = color
            self.bgChanged.emit()

    # Die Szene ruft diese vier nur aus dem Bearbeiten-Rahmen heraus, und der ist
    # hier nie sichtbar. Sie stehen trotzdem da, damit Overlay.qml unveraendert
    # in beiden Fenstern laeuft.
    @Slot(str, int, int)
    def beginEdit(self, corner: str, global_x: int, global_y: int) -> None:
        pass

    @Slot(int, int)
    def editTo(self, global_x: int, global_y: int) -> None:
        pass

    @Slot()
    def endEdit(self) -> None:
        pass


class ObsWindow(QQuickView):
    """Undurchsichtiges Fenster in Canvas-Groesse mit der Overlay-Szene darin."""

    def __init__(self, state: dict, bridge, parent=None):
        super().__init__()
        self.state = state

        # ⚠ Eigenes Surface-Format OHNE Alphakanal, gesetzt VOR dem Erzeugen des
        # Fensters. Genau das ist der Unterschied zum HUD-Fenster: ohne Alpha macht
        # Windows daraus ein gewoehnliches Fenster mit Umleitungsflaeche, und die
        # Fensteraufnahme von OBS bekommt laufend neue Bilder.
        fmt = QSurfaceFormat(self.format())
        fmt.setAlphaBufferSize(0)
        self.setFormat(fmt)

        self.setTitle("KERS OBS")
        self.setResizeMode(QQuickView.ResizeMode.SizeRootObjectToView)
        self.setFlags(Qt.WindowType.Window)

        color = str(state.get("obs_chroma_color") or "#FF00FF")
        self.setColor(QColor(color))

        self.theme = Theme(self)
        self.theme.set_opaque(True)          # Panels deckend, siehe Modulkommentar
        self.bridge = bridge
        self.hud = ObsController(color, self)

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

        self.resize(int(state.get("obs_w") or DEFAULT_W),
                    int(state.get("obs_h") or DEFAULT_H))
        self.setSource(QUrl.fromLocalFile(str(QML_DIR / "Overlay.qml")))

    def _sync_theme(self) -> None:
        self.theme.apply_settings(self.bridge.settings)
        self.theme.set_opaque(True)          # apply_settings darf das nicht kippen

    def _on_status(self, status) -> None:
        if status == QQuickView.Status.Error:
            for err in self.errors():
                print(f"[OBS] QML-Fehler: {err.toString()}")

    def reload_scene(self) -> None:
        self.engine().clearComponentCache()
        self.setSource(QUrl.fromLocalFile(str(QML_DIR / "Overlay.qml")))

    def set_chroma_color(self, color: str) -> None:
        self.state["obs_chroma_color"] = color
        self.setColor(QColor(color))
        self.hud.set_color(color)

    def set_canvas(self, w: int, h: int) -> None:
        self.state["obs_w"] = int(w)
        self.state["obs_h"] = int(h)
        self.resize(int(w), int(h))

    def is_layered(self) -> bool:
        """Diagnose: hat das Fenster doch WS_EX_LAYERED?

        Wenn ja, waere es fuer die Fensteraufnahme genauso unbrauchbar wie das
        HUD-Fenster - dann stimmt am Surface-Format etwas nicht.
        """
        if sys.platform != "win32":
            return False
        user32 = ctypes.windll.user32
        get = getattr(user32, "GetWindowLongPtrW", None) or user32.GetWindowLongW
        get.restype = ctypes.c_ssize_t
        get.argtypes = [ctypes.c_void_p, ctypes.c_int]
        return bool(get(ctypes.c_void_p(int(self.winId())), -20) & 0x00080000)
