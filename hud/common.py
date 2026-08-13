"""
Gemeinsames fuer HUD-Fenster und Steuerung: gespeicherter Zustand, Icon, Beschriftungen.

Die Baustein-Beschriftungen sind 1:1 aus templates/settings.html uebernommen
(TOGGLE_GROUPS dort) - damit steht in der Steuerung dasselbe wie auf deiner
Settings-Seite und du musst nicht zweimal ueberlegen, was was ist.
"""

import json
import sys
from pathlib import Path

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import (QBitmap, QColor, QIcon, QImage, QPainter, QPen,
                           QPixmap, QRegion)

# Als EXE (PyInstaller) liegt __file__ im temporären Entpack-Ordner, der beim
# Beenden gelöscht wird. hud_state.json (Fensterposition, Renderer-Wahl, ...)
# muss daher neben der EXE liegen, sonst ist der Zustand nach jedem Neustart weg.
if getattr(sys, "frozen", False):
    HERE = Path(sys.executable).resolve().parent
else:
    HERE = Path(__file__).resolve().parent
STATE_FILE = HERE / "hud_state.json"

# Die mitgebuendelten Bilder liegen dagegen NEBEN dem Code - als EXE also im
# Entpack-Ordner unter sys._MEIPASS, sonst eine Ebene ueber hud/. Gleiche
# Unterscheidung wie in theme.py.
if getattr(sys, "frozen", False):
    ASSET_ROOT = Path(sys._MEIPASS)          # noqa: SLF001 - so heisst es nun mal
else:
    ASSET_ROOT = Path(__file__).resolve().parent.parent
BRAND_ICON = ASSET_ROOT / "static" / "brand" / "Icon.png"

# ── Gespeicherter Zustand ─────────────────────────────────────────────────────
DEFAULT_STATE = {
    "base_url": "http://127.0.0.1:5100",
    "page": "/",             # welche Seite im HUD-Fenster haengt (nur Renderer "web")
    "renderer": "qml",       # "qml" = QML-Szene, "web" = die alte WebEngine-Seite
    "x": 60,
    "y": 60,
    "w": 780,
    "h": 900,
    "locked": False,         # Start entsperrt, sonst findet man das Fenster nicht
    "visible": True,
    "opacity": 100,          # Fenster-Deckkraft in Prozent
    "zoom": 1.0,             # Zoomfaktor der Seite (nur Renderer "web")
    "hz": 30,                # Overlay-Renderrate (siehe HZ_CHOICES)
    # OBS. Alles dazu steht in README.md unter "Overlay in OBS holen".
    #   obs_window    eigenes, undurchsichtiges Fenster in Canvas-Groesse -
    #                 der zuverlaessige Weg, siehe hud/obs_window.py
    #   obs_w/obs_h   dessen Groesse (die OBS-Canvas, nicht der Desktop)
    #   obs_findable  Qt.Tool am HUD-FENSTER weglassen, damit OBS es auflistet
    #   obs_chroma    HUD-Fenster einfarbig statt durchsichtig
    "obs_window": False,
    "obs_w": 1920,
    "obs_h": 1080,
    "obs_findable": False,
    "obs_chroma": False,
    "obs_chroma_color": "#FF00FF",
    "panel_x": 900,
    "panel_y": 60,
    "panel_visible": True,
    "panel_on_top": False,
}

# Die beiden Renderer. "web" bleibt vorerst, damit ein fertig portierter Baustein
# jederzeit gegen das Original gehalten werden kann - und als Rueckfallebene,
# solange in QML noch nicht alles drin ist.
RENDERERS = [
    ("qml", "QML - die neue Szene"),
    ("web", "Web - die HTML-Seiten (WebEngine)"),
]


def load_state() -> dict:
    state = dict(DEFAULT_STATE)
    try:
        with open(STATE_FILE, encoding="utf-8") as f:
            saved = json.load(f)
        state.update({k: v for k, v in saved.items() if k in DEFAULT_STATE})
    except Exception:
        pass
    return state


def save_state(state: dict) -> None:
    try:
        with open(STATE_FILE, "w", encoding="utf-8") as f:
            json.dump({k: state[k] for k in DEFAULT_STATE if k in state}, f, indent=2)
    except Exception as e:
        print(f"[HUD] State speichern fehlgeschlagen: {e}")


# ── Icon ──────────────────────────────────────────────────────────────────────
_ICON_CACHE = None


def make_icon() -> QIcon:
    """Das Blitz-Logo als Tray-, Fenster- und Taskleisten-Icon.

    Quelle ist static/brand/Icon.png. Fehlt die Datei - etwa weil ein Build sie
    nicht mitgebuendelt hat -, faellt es auf das gemalte Ersatz-Icon zurueck:
    lieber ein schlichtes rotes K als gar kein Icon (ohne Icon zeigt Windows das
    Standard-Programmsymbol, und im Tray sieht man dann praktisch nichts).

    ⚠ Der transparente Rand wird abgeschnitten. Das PNG ist 500x500, der Blitz
    darin nur 305x473 - gut zwei Fuenftel der Flaeche sind leer. Ungeschnitten
    rechnet Windows das GANZE Quadrat auf die 16 px des Tray-Symbols herunter und
    vom Blitz bleibt ein Fussel uebrig.

    ⚠ Der Zuschnitt MUSS danach wieder quadratisch aufgefuellt werden (_square).
    305x473 ist hochkant; Taskleiste, Titelleiste und Tray zeichnen aber in ein
    quadratisches Feld und ziehen das Bild dafuer in die Breite - der Blitz sah
    gestaucht aus. Und die fertigen Groessen kommen einzeln ins QIcon, sonst
    skaliert Windows die 473-px-Fassung selbst auf 16 px herunter, mit sichtbar
    ausgefransten Kanten.
    """
    global _ICON_CACHE
    if _ICON_CACHE is not None:
        return _ICON_CACHE

    if not BRAND_ICON.is_file():
        print(f"[HUD] Icon fehlt: {BRAND_ICON}")
        _ICON_CACHE = _fallback_icon()
        return _ICON_CACHE

    image = QImage(str(BRAND_ICON))
    if image.isNull():
        print(f"[HUD] Icon nicht lesbar: {BRAND_ICON}")
        _ICON_CACHE = _fallback_icon()
        return _ICON_CACHE

    box = _content_bounds(image)
    if box is not None:
        image = image.copy(box)
    _ICON_CACHE = _icon_from_image(_square(image))
    return _ICON_CACHE


def _square(image: QImage) -> QImage:
    """Das Bild mittig in ein Quadrat legen, ohne es zu verzerren."""
    side = max(image.width(), image.height())
    if image.width() == image.height():
        return image
    canvas = QImage(side, side, QImage.Format.Format_ARGB32_Premultiplied)
    canvas.fill(Qt.GlobalColor.transparent)
    p = QPainter(canvas)
    p.drawImage((side - image.width()) // 2, (side - image.height()) // 2, image)
    p.end()
    return canvas


# 16/20/24/32 sind die Groessen, die Windows fuer Titelleiste und Tray zieht,
# der Rest deckt Taskleiste und hohe Skalierung ab.
_ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def _icon_from_image(image: QImage) -> QIcon:
    """Ein QIcon mit fertig gerechneten Pixmaps in allen ueblichen Groessen."""
    base = QPixmap.fromImage(image)
    icon = QIcon()
    for size in _ICON_SIZES:
        icon.addPixmap(base.scaled(size, size,
                                   Qt.AspectRatioMode.KeepAspectRatio,
                                   Qt.TransformationMode.SmoothTransformation))
    return icon


def _content_bounds(image: QImage) -> QRect | None:
    """Das kleinste Rechteck, das alle nicht-durchsichtigen Punkte enthaelt.

    ⚠ Die Arbeit macht Qt, nicht Python: createAlphaMask() liefert eine 1-Bit-Maske,
    und QRegion.boundingRect() misst sie in C++ aus. Dasselbe Ergebnis Pixel fuer
    Pixel in Python zu suchen hat bei diesem 500x500-Bild 150 ms gekostet - bei
    jedem Programmstart, nur fuer ein Icon.
    """
    region = QRegion(QBitmap.fromImage(image.createAlphaMask()))
    rect = region.boundingRect()
    if rect.isEmpty():
        return None                      # komplett durchsichtig - nichts zu holen
    return rect


def _fallback_icon() -> QIcon:
    """Ersatz-Icon, zur Laufzeit gemalt - haengt an keiner Datei."""
    pm = QPixmap(64, 64)
    pm.fill(Qt.GlobalColor.transparent)
    p = QPainter(pm)
    p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor("#e10600"))
    p.drawRoundedRect(4, 4, 56, 56, 14, 14)
    p.setPen(QPen(QColor("#ffffff")))
    font = p.font()
    font.setBold(True)
    font.setPixelSize(38)
    p.setFont(font)
    p.drawText(pm.rect(), Qt.AlignmentFlag.AlignCenter, "K")
    p.end()
    return QIcon(pm)


# ── Seiten, die ins HUD-Fenster koennen ───────────────────────────────────────
# Die Bausteine kommen aus PARTS in main.py (Route /part/<name>). Sie stehen hier
# mit drin, damit du das HUD notfalls auch auf einen einzelnen Baustein setzen
# kannst - Standard bleibt aber das Gesamt-Overlay.
PART_LABELS = [
    ("tower",      "Timing Tower"),
    ("battles",    "Battle-Boxen"),
    ("hotlap",     "Hotlap-Box (Quali)"),
    ("trackmap",   "Trackmap"),
    ("onboard",    "Onboard-Telemetrie"),
    ("lowerthird", "Lower-Third"),
    ("pit",        "Boxenstopp"),
    ("lights",     "Start-Ampel"),
    ("flbanner",   "Fastest-Lap-Banner"),
    ("undercut",   "Undercut-Alarm"),
    ("danger",     "Gefahrenzone (Quali)"),
    ("champ",      "WM-Stand"),
    ("pitproj",    "Pit-Projektion"),
    ("racemsg",    "Rennleitungs-Meldungen"),
    ("charts",     "Verlaufs-Charts"),
]

PAGES = (
    [("Gesamt-Overlay - Produktion  (/)", "/"),
     ("Gesamt-Overlay - Spielwiese  (/test)", "/test"),
     ("Gesamt-Overlay - Sparfassung  (/opti)", "/opti")]
    + [(f"Baustein: {label}", f"/part/{key}") for key, label in PART_LABELS]
)


# ── Overlay-Renderrate ────────────────────────────────────────────────────────
# Nur /opti wertet das aus; die anderen Seiten ignorieren den Parameter.
#
# Obergrenze ist das Spiel: F1 sendet hoechstens 60 Pakete/s. Darueber aendern
# sich die Daten nicht mehr, und 120 wirkt nur noch auf die weich hochzaehlenden
# Pit- und Hotlap-Zeiten. Deshalb steht das in der Beschriftung mit dran.
HZ_CHOICES = [
    (10,  "10 Hz - sehr sparsam"),
    (20,  "20 Hz"),
    (30,  "30 Hz - Standard"),
    (60,  "60 Hz - so schnell wie das Spiel sendet"),
    (120, "120 Hz - nur Zaehler laufen weicher"),
]


# Absichtlich NICHT hier: die Baustein-Schalter, die Regie-Einblendungen und die
# Regler aus /settings. Das Schaltbrett startet den Server und platziert das HUD -
# eingestellt wird in /settings und /regie, und die bleiben die einzige Wahrheit.
