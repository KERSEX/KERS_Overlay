"""
Das eigentliche HUD-Fenster: dein Overlay als Always-on-Top-Fenster auf dem Desktop.

Die Fenster-Technik ist dieselbe, die Pits N' Giggles benutzt
(apps/hud/ui/overlays/base/base_overlay.py -> update_window_flags):

    Qt.FramelessWindowHint        kein Rahmen, keine Titelleiste
    Qt.WindowStaysOnTopHint       bleibt ueber allen anderen Fenstern
    Qt.Tool                       kein Taskbar- und kein Alt-Tab-Eintrag
    Qt.WindowTransparentForInput  Klicks gehen durch (nur im gesperrten Zustand)
    WA_TranslucentBackground      echter Alpha-Kanal statt schwarzem Kasten

Der Inhalt ist dein unveraendertes HTML aus templates/ + static/ - hier laeuft
nur eine WebEngineView drum herum. Du baust also weiter ganz normal in HTML/CSS/JS.

WICHTIG: Ueber einem exklusiven Vollbild kann Windows grundsaetzlich kein Fenster
legen. F1 muss auf "Rahmenloses Fenster" stehen, sonst ist das HUD unsichtbar -
das gilt fuer jedes Overlay dieser Bauart, auch fuer Pits N' Giggles selbst.
"""

from PySide6.QtCore import QPoint, QRect, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWebEngineCore import QWebEnginePage, QWebEngineSettings
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import QLabel, QVBoxLayout, QWidget

MIN_W, MIN_H = 240, 160


class EditGlass(QWidget):
    """Transparente Scheibe UEBER der WebEngineView (Bearbeiten-Modus).

    Noetig, weil die WebEngineView selbst alle Mausereignisse schluckt - ohne
    diese Schicht koennte man das Fenster nie greifen. Entspricht der
    OverlayBorder.qml aus Pits N' Giggles (dort mit z: 9999 ueber den Inhalt).
    Sichtbar nur, solange das HUD entsperrt ist.
    """

    HANDLE = 18

    CURSORS = {
        "tl": Qt.CursorShape.SizeFDiagCursor,
        "br": Qt.CursorShape.SizeFDiagCursor,
        "tr": Qt.CursorShape.SizeBDiagCursor,
        "bl": Qt.CursorShape.SizeBDiagCursor,
    }

    def __init__(self, host: "OverlayWindow"):
        super().__init__(host)
        self._host = host
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        self.setMouseTracking(True)
        self._drag_offset = None
        self._resize_corner = None
        self._origin_geo = None
        self._origin_mouse = None

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        pen = QPen(QColor("#e10600"))
        pen.setWidth(2)
        pen.setStyle(Qt.PenStyle.DashLine)
        p.setPen(pen)
        p.drawRect(self.rect().adjusted(1, 1, -2, -2))

        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor("#e10600"))
        for rect in self._corner_rects().values():
            p.drawRect(rect)

        p.setPen(QPen(QColor("#ffffff")))
        font = p.font()
        font.setPixelSize(12)
        font.setBold(True)
        p.setFont(font)
        hint = "Bearbeiten - ziehen = verschieben, Ecken = Groesse, Doppelklick = sperren"
        box = QRect(self.HANDLE + 6, 4, max(0, self.width() - 2 * self.HANDLE - 12), 20)
        p.fillRect(box.adjusted(-4, 0, 4, 0), QColor(0, 0, 0, 170))
        p.drawText(box, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, hint)
        p.end()

    def _corner_rects(self) -> dict:
        h = self.HANDLE
        w, ht = self.width(), self.height()
        return {
            "tl": QRect(0, 0, h, h),
            "tr": QRect(w - h, 0, h, h),
            "bl": QRect(0, ht - h, h, h),
            "br": QRect(w - h, ht - h, h, h),
        }

    def _corner_at(self, pos: QPoint):
        for name, rect in self._corner_rects().items():
            if rect.contains(pos):
                return name
        return None

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        self._origin_mouse = event.globalPosition().toPoint()
        self._origin_geo = self._host.geometry()
        corner = self._corner_at(event.position().toPoint())
        if corner:
            self._resize_corner = corner
            self._drag_offset = None
        else:
            self._resize_corner = None
            self._drag_offset = self._origin_mouse - self._host.pos()

    def mouseMoveEvent(self, event):
        if not (event.buttons() & Qt.MouseButton.LeftButton):
            corner = self._corner_at(event.position().toPoint())
            self.setCursor(self.CURSORS[corner] if corner else Qt.CursorShape.SizeAllCursor)
            return
        gpos = event.globalPosition().toPoint()
        if self._resize_corner:
            self._apply_resize(gpos)
        elif self._drag_offset is not None:
            self._host.move(gpos - self._drag_offset)

    def mouseReleaseEvent(self, _event):
        self._drag_offset = None
        self._resize_corner = None
        self._host.remember_geometry()

    def mouseDoubleClickEvent(self, _event):
        self._host.set_locked(True)

    def _apply_resize(self, gpos: QPoint) -> None:
        """Ecke ziehen: die gegenueberliegende Ecke bleibt stehen."""
        delta = gpos - self._origin_mouse
        origin = self._origin_geo
        x, y = origin.x(), origin.y()
        w, h = origin.width(), origin.height()

        if "l" in self._resize_corner:
            x = origin.x() + delta.x()
            w = origin.width() - delta.x()
        else:
            w = origin.width() + delta.x()

        if "t" in self._resize_corner:
            y = origin.y() + delta.y()
            h = origin.height() - delta.y()
        else:
            h = origin.height() + delta.y()

        if w < MIN_W:
            w = MIN_W
            if "l" in self._resize_corner:
                x = origin.x() + origin.width() - MIN_W
        if h < MIN_H:
            h = MIN_H
            if "t" in self._resize_corner:
                y = origin.y() + origin.height() - MIN_H

        self._host.setGeometry(x, y, w, h)


class OverlayWindow(QWidget):
    """Rahmenloses Always-on-Top-Fenster mit der Overlay-Seite darin."""

    # Bewusst mit "hud"-Praefix: geometryChanged/visibilityChanged heissen auf
    # QWindow bereits so - auf QWidget nicht, aber Verwechslung waere unnoetig.
    hudGeometryChanged = Signal(int, int, int, int)   # x, y, w, h
    hudLockedChanged = Signal(bool)
    hudVisibilityChanged = Signal(bool)
    pageLoaded = Signal(bool)                        # True = geladen, False = kein Kontakt

    RETRY_FAST_MS = 3000       # erste Versuche: schnell, der Server kommt meist gleich
    RETRY_SLOW_MS = 30000      # danach: der Server ist offenbar wirklich aus
    RETRY_FAST_COUNT = 10      # so viele schnelle Versuche (= gut 30 Sekunden)

    def __init__(self, state: dict):
        super().__init__()
        self.state = state
        self._blanking = False
        self._want_visible = bool(state["visible"])

        self.setWindowTitle("KERS HUD")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground, True)

        self.view = QWebEngineView(self)
        # Ohne das malt Chromium einen weissen Grund und die Transparenz aus
        # deinem CSS (body { background-color: transparent }) verpufft.
        self.view.page().setBackgroundColor(Qt.GlobalColor.transparent)
        self.view.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        self.view.setStyleSheet("background: transparent;")

        settings = self.view.settings()
        settings.setAttribute(QWebEngineSettings.WebAttribute.ShowScrollBars, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.ScrollAnimatorEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.LocalContentCanAccessRemoteUrls, True)
        # Was das Overlay nachweislich nicht benutzt, muss Chromium auch nicht bereithalten.
        # (Im Overlay gibt es kein <canvas> und kein WebGL - nur DOM, CSS und ein SVG.)
        settings.setAttribute(QWebEngineSettings.WebAttribute.WebGLEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.Accelerated2dCanvasEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.PluginsEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.AutoLoadIconsForPage, False)

        self.view.setZoomFactor(float(state["zoom"]))
        self.view.loadFinished.connect(self._on_load_finished)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.view)

        self.status = QLabel(self)
        self.status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status.setWordWrap(True)
        self.status.setStyleSheet(
            "background: rgba(14,14,19,0.94); color: #fff;"
            "border: 1px solid rgba(255,255,255,0.09); border-radius: 10px;"
            "padding: 14px; font-family: 'Segoe UI'; font-size: 13px;"
        )
        self.status.hide()

        self.glass = EditGlass(self)
        self.glass.raise_()

        # Nachlade-Versuche: erst schnell, dann langsam. Ohne die zweite Stufe laedt
        # die Seite bei laengerem Server-Ausfall stundenlang alle 3 s neu.
        self._retry = QTimer(self)
        self._retry.setInterval(self.RETRY_FAST_MS)
        self._retry.timeout.connect(self.reload_page)
        self._retry_count = 0

        self.setGeometry(state["x"], state["y"], state["w"], state["h"])
        self.setWindowOpacity(state["opacity"] / 100.0)
        self.update_window_flags()
        self.reload_page()

    # -- URL ----------------------------------------------------------------
    @property
    def url(self) -> str:
        """Seiten-URL inkl. ?hz=N.

        Der Parameter steht bewusst in der URL und wird nicht nur per JavaScript
        gesetzt: so gilt die Rate schon beim ALLERSTEN Bild nach dem Laden, nicht
        erst wenn das Fenster hinterher nachschiebt. Seiten, die ihn nicht kennen
        (/, /test, /part/...), ignorieren ihn einfach.
        """
        base = f"{self.state['base_url'].rstrip('/')}{self.state['page']}"
        sep = "&" if "?" in base else "?"
        return f"{base}{sep}hz={int(self.state.get('hz', 30))}"

    def set_page(self, path: str) -> None:
        self.state["page"] = path
        self.reload_page()

    def set_base_url(self, base: str) -> None:
        self.state["base_url"] = base.rstrip("/")
        self.reload_page()

    # -- Renderrate ----------------------------------------------------------
    def set_hz(self, hz: int) -> None:
        """Overlay-Hz umstellen - ohne Neuladen, damit man es live vergleichen kann."""
        self.state["hz"] = int(hz)
        self.apply_hz()

    def apply_hz(self) -> None:
        """Rate an die Seite durchreichen.

        Seiten ohne KERS.setHz (also /, /test und die /part/-Seiten) tun hier
        nichts - der Aufruf ist bewusst so geschrieben, dass er dort still bleibt
        statt einen Fehler in die Konsole zu schreiben.
        """
        hz = int(self.state.get("hz", 30))
        self.view.page().runJavaScript(
            f"window.KERS && KERS.setHz && KERS.setHz({hz});"
        )

    def reload_page(self) -> None:
        self._blanking = False
        self.view.setUrl(QUrl(self.url))

    def _on_load_finished(self, ok: bool) -> None:
        if self._blanking:
            self._blanking = False
            return
        if ok:
            self.status.hide()
            self._retry.stop()
            self._retry_count = 0
            self.apply_hz()          # Renderrate der frisch geladenen Seite setzen
            self.pageLoaded.emit(True)
            return
        # Chromium wuerde jetzt seine eigene (undurchsichtige) Fehlerseite zeigen
        self._blanking = True
        self.view.setUrl(QUrl("about:blank"))

        self._retry_count += 1
        if self._retry_count > self.RETRY_FAST_COUNT:
            self._retry.setInterval(self.RETRY_SLOW_MS)
        wait = self._retry.interval() // 1000

        self.status.setText(
            f"Kein Kontakt zu {self.url}\n\n"
            f"Laeuft main.py schon?\nNaechster Versuch in {wait} Sekunden."
        )
        self._place_status()
        self.status.show()
        self.status.raise_()
        self._retry.start()
        self.pageLoaded.emit(False)

    def _place_status(self) -> None:
        self.status.setFixedWidth(min(420, max(240, self.width() - 40)))
        self.status.adjustSize()
        self.status.move(
            max(0, (self.width() - self.status.width()) // 2),
            max(0, (self.height() - self.status.height()) // 2),
        )

    # -- Fensterflags -------------------------------------------------------
    def update_window_flags(self) -> None:
        flags = (
            Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.Tool
        )
        if self.state["locked"]:
            flags |= Qt.WindowType.WindowTransparentForInput

        geo = self.geometry()
        self.setWindowFlags(flags)   # macht das Fenster unter Windows unsichtbar
        self.setGeometry(geo)        # ... und vergisst dabei gerne die Geometrie
        self.glass.setVisible(not self.state["locked"])
        if self._want_visible:
            self.show()

    def set_locked(self, locked: bool) -> None:
        self.state["locked"] = bool(locked)
        self.update_window_flags()
        self.hudLockedChanged.emit(self.state["locked"])

    def toggle_locked(self) -> None:
        self.set_locked(not self.state["locked"])

    def set_hud_visible(self, visible: bool) -> None:
        self._want_visible = bool(visible)
        self.state["visible"] = self._want_visible
        self.setVisible(self._want_visible)
        self._set_page_frozen(not self._want_visible)
        self.hudVisibilityChanged.emit(self._want_visible)

    def _set_page_frozen(self, frozen: bool) -> None:
        """Seite anhalten, solange das HUD ausgeblendet ist.

        Ein verstecktes Fenster laesst Chromium sonst munter weiterlaufen: Timer,
        SSE-Empfang und Renderarbeit gehen unveraendert weiter, obwohl niemand
        hinsieht. "Frozen" haelt genau das an; beim Einblenden laeuft es weiter
        und der naechste SSE-Payload bringt den Stand wieder auf aktuell.

        Bewusst NICHT "Discarded": das wuerde die Seite wegwerfen und beim
        Einblenden einen kompletten Neuaufbau samt Nachladen ausloesen.
        """
        page = self.view.page()
        target = (QWebEnginePage.LifecycleState.Frozen if frozen
                  else QWebEnginePage.LifecycleState.Active)
        try:
            if page.lifecycleState() != target:
                page.setLifecycleState(target)
        except Exception as e:  # pylint: disable=broad-exception-caught
            # Nicht kritisch - dann laeuft die Seite eben weiter wie bisher.
            print(f"[HUD] Lifecycle-Wechsel nicht moeglich: {e}")

    # Deckkraft und Zoom haben bewusst keine Bedienelemente mehr - sie werden
    # einmal beim Start aus hud_state.json uebernommen ("opacity", "zoom") und
    # sind dort auch der Ort, an dem man sie aendert. Im Betrieb braucht man sie
    # nicht, und das Schaltbrett bleibt so auf dem, was es koennen soll.

    def apply_geometry(self, x: int, y: int, w: int, h: int) -> None:
        self.setGeometry(int(x), int(y), max(MIN_W, int(w)), max(MIN_H, int(h)))
        self.remember_geometry()

    # -- Geometrie ----------------------------------------------------------
    def remember_geometry(self) -> None:
        geo = self.geometry()
        self.state.update({"x": geo.x(), "y": geo.y(),
                           "w": geo.width(), "h": geo.height()})
        self.hudGeometryChanged.emit(geo.x(), geo.y(), geo.width(), geo.height())

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.glass.setGeometry(self.rect())
        self._place_status()
        self.remember_geometry()

    def moveEvent(self, event):
        super().moveEvent(event)
        self.remember_geometry()
