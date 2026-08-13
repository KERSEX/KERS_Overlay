"""
Das Gegenstueck zu :root in static/css/core.css.

Im Web-Overlay stehen alle Farben, Radien und Schattenwerte als CSS-Custom-Properties
im :root, und applyBrand() in core.js schreibt zur Laufzeit hinein: die Akzentfarbe
aus den Settings, die Deckkraft (--ui-alpha), die Kopf- und Zeilenfarbe. Jede Regel im
CSS liest sie ueber var().

Genau das macht diese Klasse: ein Objekt mit allen Tokens, das QML als Singleton
`Theme` sieht. Setzt die Bruecke `uiAlpha` oder `accent`, faerbt sich die ganze Szene
mit um - dieselbe Mechanik wie im Browser, nur mit Qt-Signalen statt CSS-Kaskade.

Warum Singleton und nicht Context-Property: eine Context-Property muss QML bei jedem
Binding ueber die Kontextkette suchen. In einer Fahrerzeile, die 22-mal existiert und
30-mal pro Sekunde neu rechnet, ist das messbar. Singletons loest die Engine direkt auf.

⚠ Aendert sich etwas in core.css, muss es auch hier stehen - solange beide Overlays
nebeneinander laufen, sollen sie gleich aussehen.
"""

import sys
from pathlib import Path

from PySide6.QtCore import Property, QObject, QUrl, Signal, Slot
from PySide6.QtGui import QColor, QFontDatabase

# Als EXE liegen die gebuendelten Daten (static/) unter sys._MEIPASS, nicht
# relativ zu __file__ (das zeigt in den Pyinstaller-Temp-Ordner).
if getattr(sys, "frozen", False):
    ROOT = Path(sys._MEIPASS)
else:
    ROOT = Path(__file__).resolve().parent.parent
STATIC = ROOT / "static"
FONT_DIR = STATIC / "fonts" / "ttf"

# Zeilenhoehe des Towers - identisch zu ROW_HEIGHT in static/parts/tower.js.
ROW_HEIGHT = 62


def _rgba(r: int, g: int, b: int, a: float) -> QColor:
    c = QColor(r, g, b)
    c.setAlphaF(max(0.0, min(1.0, a)))
    return c


def load_fonts() -> list:
    """Inter und Teko aus static/fonts/ttf/ in die Fontdatenbank laden.

    Die woff2 aus dem Web-Overlay kann Qt nicht - die TTF erzeugt einmalig
    tools/woff2_to_ttf.py. Fehlen sie, faellt Qt auf die Systemschrift zurueck;
    das Overlay laeuft dann, sieht aber anders aus. Deshalb die Warnung.
    """
    loaded = []
    if not FONT_DIR.is_dir():
        print(f"[HUD] Schriften fehlen: {FONT_DIR}\n"
              f"      Einmalig erzeugen:  python tools/woff2_to_ttf.py")
        return loaded
    for path in sorted(FONT_DIR.glob("*.ttf")):
        if QFontDatabase.addApplicationFont(str(path)) == -1:
            print(f"[HUD] Schrift nicht ladbar: {path.name}")
        else:
            loaded.append(path.name)
    if not loaded:
        print(f"[HUD] Keine Schrift geladen - Overlay nutzt die Systemschrift.")
    return loaded


class Theme(QObject):
    """Farben, Masse und Asset-Pfade fuer die QML-Szene."""

    changed = Signal()          # alles, was von uiAlpha oder Branding abhaengt

    # Feste Werte aus core.css - keine Signale noetig, die aendern sich nie.
    _TEXT_MAIN = "#ffffff"
    _TEXT_MUTED = "#a6a6b4"
    _ACCENT_DEFAULT = "#e10600"

    TEAM_COLORS = {
        "Red Bull": "#3671C6", "McLaren": "#FF8000", "Ferrari": "#E80020",
        "Mercedes": "#27F4D2", "Aston Martin": "#229971", "Haas": "#B6BABD",
        "RB": "#6692FF", "Alpine": "#0093CC", "Williams": "#64C4FF",
        "Sauber": "#52E252", "Audi": "#C8102E", "Cadillac": "#C9A227",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._ui_alpha = 1.0
        self._opaque = False
        self._accent = self._ACCENT_DEFAULT
        self._header_accent = ""      # leer = Akzentfarbe verwenden
        self._row_override = ""       # leer = die beiden Standard-Zeilentoene
        self._asset_root = QUrl.fromLocalFile(str(STATIC)).toString().rstrip("/")

    @property
    def _alpha(self) -> float:
        """Die tatsaechlich verwendete Deckkraft.

        Im Chroma-Key-Modus muessen ALLE Flaechen deckend sein. Sonst scheint die
        Schluesselfarbe durch die halbtransparenten Panels, der Farb-Key in OBS
        frisst sie anteilig mit weg und das Overlay bekommt einen Farbstich.
        1.25 ist derselbe Wert, den core.css als "ab hier komplett deckend" nennt.
        """
        return 1.25 if self._opaque else self._ui_alpha

    def set_opaque(self, opaque: bool) -> None:
        if self._opaque != bool(opaque):
            self._opaque = bool(opaque)
            self.changed.emit()

    # ── Von der Bruecke gesetzt (Gegenstueck zu applyBrand in core.js) ───────
    def apply_settings(self, settings) -> None:
        alpha = float(settings.uiAlpha)
        accent = settings.brandAccent or self._ACCENT_DEFAULT
        header = settings.headerColor or ""
        row = settings.rowColor or ""
        if (alpha, accent, header, row) == (self._ui_alpha, self._accent,
                                            self._header_accent, self._row_override):
            return
        self._ui_alpha = alpha
        self._accent = accent if QColor(accent).isValid() else self._ACCENT_DEFAULT
        self._header_accent = header if QColor(header).isValid() else ""
        self._row_override = row if QColor(row).isValid() else ""
        self.changed.emit()

    # ── Deckkraft ────────────────────────────────────────────────────────────
    @Property(float, notify=changed)
    def uiAlpha(self):
        return self._ui_alpha

    # ── Grundfarben ──────────────────────────────────────────────────────────
    @Property(QColor, notify=changed)
    def accent(self):
        """--accent-red: die Akzentfarbe aus den Settings."""
        return QColor(self._accent)

    @Property(QColor, notify=changed)
    def accentGlow(self):
        c = QColor(self._accent)
        c.setAlphaF(0.5)
        return c

    @Property(QColor, notify=changed)
    def headerAccent(self):
        """--header-accent: eigene Farbe fuer den Streifen unter dem Kopf, sonst Akzent."""
        return QColor(self._header_accent or self._accent)

    @Property(QColor, constant=True)
    def textMain(self):
        return QColor(self._TEXT_MAIN)

    @Property(QColor, constant=True)
    def textMuted(self):
        return QColor(self._TEXT_MUTED)

    # ── Flaechen (alle haengen an --ui-alpha) ────────────────────────────────
    @Property(QColor, notify=changed)
    def panelBg(self):
        return _rgba(14, 14, 19, 0.94 * self._alpha)

    @Property(QColor, notify=changed)
    def rowBg(self):
        if self._row_override:
            return QColor(self._row_override)
        return _rgba(24, 24, 33, 0.988 * self._alpha)

    @Property(QColor, notify=changed)
    def rowBgAlt(self):
        if self._row_override:
            return QColor(self._row_override)
        return _rgba(16, 16, 23, 0.988 * self._alpha)

    @Property(QColor, notify=changed)
    def headerRowBg(self):
        """.header-row - enthaelt den weggefallenen Container-Hintergrund mit."""
        return _rgba(10, 10, 15, 0.994 * self._alpha)

    @Property(QColor, notify=changed)
    def towerHead1(self):
        return _rgba(22, 22, 30, self._alpha)

    @Property(QColor, notify=changed)
    def towerHead2(self):
        return _rgba(40, 40, 50, self._alpha)

    @Property(QColor, notify=changed)
    def headBg1(self):
        return _rgba(27, 27, 34, self._alpha)

    @Property(QColor, notify=changed)
    def headBg2(self):
        return _rgba(17, 17, 22, self._alpha)

    @Property(QColor, constant=True)
    def panelBorder(self):
        return _rgba(255, 255, 255, 0.09)

    # ── Masse ────────────────────────────────────────────────────────────────
    @Property(int, constant=True)
    def panelRadius(self):
        return 10

    @Property(int, constant=True)
    def accentWidth(self):
        """--accent: Dicke der Akzentkante unter dem Tower-Kopf."""
        return 3

    @Property(int, constant=True)
    def rowHeight(self):
        return ROW_HEIGHT

    # ── Schriften ────────────────────────────────────────────────────────────
    @Property(str, constant=True)
    def sans(self):
        """Flaechenschrift - im CSS 'Inter', sans-serif."""
        return "Inter"

    @Property(str, constant=True)
    def display(self):
        """Auszeichnungsschrift - im CSS 'Teko', sans-serif (Positionen, Titel, Badges)."""
        return "Teko"

    # ── Asset-Pfade ──────────────────────────────────────────────────────────
    # Absolute file:-URLs, von Python gesetzt. Relative Pfade aus den QML-Dateien
    # heraus waeren an die Ordnertiefe der jeweiligen Datei gebunden und brechen,
    # sobald ein Baustein in einen Unterordner wandert.
    @Property(str, constant=True)
    def assetRoot(self):
        return self._asset_root

    @Slot(str, result=str)
    def teamLogo(self, filename: str) -> str:
        return f"{self._asset_root}/teams/{filename}.png" if filename else ""

    @Slot(str, result=str)
    def tyre(self, compound: str) -> str:
        return f"{self._asset_root}/tyres/{compound}.png" if compound else ""

    @Slot(str, result=str)
    def damage(self, part: str) -> str:
        return f"{self._asset_root}/damage/{part}.png" if part else ""

    @Slot(str, result=str)
    def brandLogo(self, filename: str) -> str:
        """Eigenes Logo im Tower-Kopf (static/logos/, Auswahl in den Settings)."""
        return f"{self._asset_root}/logos/{QUrl.toPercentEncoding(filename).data().decode()}" \
            if filename else ""

    @Slot(str, result=QColor)
    def teamColor(self, team: str) -> QColor:
        return QColor(self.TEAM_COLORS.get(team, "#ffffff"))
