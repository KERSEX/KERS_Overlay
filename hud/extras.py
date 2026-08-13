"""
Die drei Bausteine mit eigenen Server-Abfragen: Trackmap, Charts, WM-Stand.

Anders als der Rest holen sie sich Daten, die NICHT im SSE-Strom stecken:

    Trackmap   /api/track          die live gelernte Streckenkontur
    Charts     /api/lap_positions  Positionen je Runde (Paket 15)
    WM-Stand   /api/championship   WM-Punkte plus Live-Hochrechnung

Alle drei laufen ueber QNetworkAccessManager - also asynchron im Qt-Eventloop, wie
api_client.py. Ein blockierender Aufruf wuerde hier das Overlay anhalten.

⚠ WARUM DIE GEOMETRIE IN PYTHON GERECHNET WIRD
Die Kontur hat auf einer 5-km-Strecke gut tausend Punkte, und jeder muss skaliert,
gedreht und gespiegelt werden (normPoint in trackmap.js). Das in QML-JavaScript zu
tun hiesse, tausend Punkte pro Bild durch den Interpreter zu schicken. Hier passiert
es einmal - und nur dann neu, wenn sich Kontur, Drehung oder Spiegelung aendern.
QML bekommt fertige Punktlisten und zeichnet sie mit QtQuick.Shapes.
"""

import json
import math

from PySide6.QtCore import (QObject, Property, QPointF, QTimer, QUrl, Signal)
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from models import TEAM_COLORS, _prop, _StateBase

# Sektorfarben der Kontur - identisch zu SECTOR_COLORS in trackmap.js.
SECTOR_COLORS = {0: "#eaeaf0", 1: "#2f7bff", 2: "#e10600"}

# So lange bleibt der Punkt eines on-track Ausgeschiedenen stehen (trackmap.js).
DNF_LINGER_MS = 12000


class _Fetcher(QObject):
    """Kleiner asynchroner GET-Helfer - dasselbe Muster wie in api_client.py."""

    def __init__(self, base_url: str, parent=None):
        super().__init__(parent)
        self._base = base_url.rstrip("/")
        self._nam = QNetworkAccessManager(self)
        self._busy = set()

    def set_base_url(self, url: str) -> None:
        self._base = url.rstrip("/")

    def get(self, path: str, on_json) -> None:
        if path in self._busy:
            return                       # noch eine Abfrage laeuft - nicht stapeln
        self._busy.add(path)
        req = QNetworkRequest(QUrl(f"{self._base}{path}"))
        req.setAttribute(QNetworkRequest.Attribute.CacheLoadControlAttribute,
                         QNetworkRequest.CacheLoadControl.AlwaysNetwork)
        reply = self._nam.get(req)

        def done():
            self._busy.discard(path)
            try:
                if reply.error() != QNetworkReply.NetworkError.NoError:
                    return
                on_json(json.loads(bytes(reply.readAll().data()).decode("utf-8")))
            except Exception:  # pylint: disable=broad-exception-caught
                pass           # Server weg oder Murks im Payload -> stiller Neuversuch
            finally:
                reply.deleteLater()

        reply.finished.connect(done)


# ── Trackmap ─────────────────────────────────────────────────────────────────
class Trackmap(_StateBase):
    """Die Minimap (trackmap.js).

    Die Kontur wird vom Server gelernt und ist erst brauchbar, wenn `done` kommt -
    vorher zeigt die Karte nichts. Punkte sind [x, z, Rundenanteil, Sektor].
    """

    POLL_MS = 2000
    STOPPED_MS = 1500        # so lange ohne Bewegung gilt ein Auto als "steht"
    STOPPED_M = 3            # ... bei weniger als dieser Strecke

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    sizeChanged, size = _prop("size", int, 400)
    contourChanged = Signal()
    carsChanged = Signal()

    def __init__(self, base_url: str, cfg, parent=None):
        super().__init__(parent)
        self._cfg = cfg
        self._fetch = _Fetcher(base_url, self)
        self._pts = []               # geglaettete Rohpunkte
        self._bounds = None
        self._ver = -1
        self._done = False
        self._cos, self._sin, self._flip = 1.0, 0.0, True
        self._fit = (1.0, 50.0, 50.0)   # (Faktor, Mittelpunkt x, Mittelpunkt z) - s. _update_fit
        self._tf_sig = ""
        self._contour = []           # [{color, points:[QPointF...]}]
        self._flags = []
        self._flag_sig = ""
        self._cars = []
        self._move = {}
        self._ghost = {}
        self._ghost_done = set()
        self._last_pos = {}

        self._timer = QTimer(self)
        self._timer.setInterval(self.POLL_MS)
        self._timer.timeout.connect(self._refresh)

    def set_base_url(self, url: str) -> None:
        self._fetch.set_base_url(url)

    def start(self) -> None:
        self._refresh()
        self._timer.start()

    def stop(self) -> None:
        self._timer.stop()

    # ── Fuer QML ─────────────────────────────────────────────────────────────
    @Property("QVariantList", notify=contourChanged)
    def contour(self):
        """Die Streckenkontur als farbige Teilstuecke im 0..100-Koordinatensystem."""
        return self._contour

    @Property("QVariantList", notify=contourChanged)
    def flags(self):
        """Gelb geflaggte Abschnitte (Marshal-Zonen)."""
        return self._flags

    @Property("QVariantList", notify=carsChanged)
    def cars(self):
        return self._cars

    # ── Kontur ───────────────────────────────────────────────────────────────
    def _refresh(self) -> None:
        self._fetch.get("/api/track", self._on_track)

    def load_points(self, pts, ver: int = 1) -> None:
        """Kontur direkt setzen, ohne den Server zu fragen.

        Braucht der Demo-Modus: dort gibt es kein /api/track, die Strecke kommt
        aus demo.py.
        """
        self._on_track({"done": True, "pts": pts, "ver": ver})

    def _on_track(self, t: dict) -> None:
        self._done = bool(t.get("done"))
        pts = t.get("pts") or []
        if not self._done or len(pts) < 20:
            return
        if t.get("ver") == self._ver:
            return
        self._ver = t.get("ver")
        self._pts = _smooth_loop(pts)

        xs = [p[0] for p in self._pts]
        zs = [p[1] for p in self._pts]
        min_x, max_x = min(xs), max(xs)
        min_z, max_z = min(zs), max(zs)
        span_x = (max_x - min_x) or 1
        span_z = (max_z - min_z) or 1
        # Grobe Vorskalierung; den genauen Sitz macht danach _update_fit().
        pad, size = 15, 70
        sc = size / max(span_x, span_z)          # EIN Faktor -> keine Verzerrung
        self._bounds = (min_x, min_z, sc,
                        pad + (size - span_x * sc) / 2,
                        pad + (size - span_z * sc) / 2)
        self._ensure_transform(force=True)
        self._update_fit()
        self._build_contour()
        self._flag_sig = ""

    def _ensure_transform(self, force: bool = False) -> bool:
        sig = f"{self._cfg['maprot'] or 0}|{1 if self._cfg['mapflip'] else 0}"
        if sig == self._tf_sig and not force:
            return False
        self._tf_sig = sig
        rad = math.radians(self._cfg["maprot"] or 0)
        self._cos, self._sin = math.cos(rad), math.sin(rad)
        self._flip = bool(self._cfg["mapflip"])
        return True

    # Wieviel vom 0..100-Feld die Strecke einnehmen darf. Der Rest ist Rand für die
    # Strichstärken (Casing 4.4 -> 2.2 Überstand) und die Auto-Punkte (r bis 3.4).
    FIT_TARGET = 88.0

    def _update_fit(self) -> None:
        """Die GEDREHTE Kontur formatfüllend in die Karte einpassen.

        Vorher stand hier nur die feste Vorskalierung (Kontur auf 70 von 100, Rand 15) —
        gross genug, dass JEDE Drehung hineinpasst. Das kostete bei einer 480-px-Karte
        72 px Leerraum je Seite, unabhängig davon, wie die Strecke tatsächlich liegt:
        die Karte schien weit vom Bildschirmrand weg. Jetzt wird die fertig gedrehte
        Kontur ausgemessen und genau eingepasst, der Rand ist nur noch so breit wie
        Linien und Punkte ihn brauchen.

        Muss nach jeder Änderung von Kontur, Drehung oder Spiegelung laufen.
        """
        self._fit = (1.0, 50.0, 50.0)
        if not self._pts or not self._bounds:
            return
        xs, zs = [], []
        for p in self._pts:
            rx, rz = self._rotated(p[0], p[1])
            xs.append(rx)
            zs.append(rz)
        span = max(max(xs) - min(xs), max(zs) - min(zs))
        if span <= 0:
            return
        self._fit = (self.FIT_TARGET / span,
                     (max(xs) + min(xs)) / 2,
                     (max(zs) + min(zs)) / 2)

    def _rotated(self, x: float, z: float):
        """Weltkoordinaten -> gedreht/gespiegelt, aber noch ohne Einpassung."""
        min_x, min_z, sc, off_x, off_z = self._bounds
        nx = off_x + (x - min_x) * sc
        nz = 100 - (off_z + (z - min_z) * sc)      # z-Achse umdrehen
        dx, dz = nx - 50, nz - 50
        rx = dx * self._cos - dz * self._sin
        rz = dx * self._sin + dz * self._cos
        if self._flip:
            rx = -rx
        return rx + 50, rz + 50

    def _norm(self, x: float, z: float):
        """Weltkoordinaten -> 0..100, gedreht, gespiegelt und eingepasst (normPoint)."""
        if not self._bounds:
            return None
        rx, rz = self._rotated(x, z)
        f, cx, cz = self._fit
        return QPointF((rx - cx) * f + 50, (rz - cz) * f + 50)

    def _build_contour(self) -> None:
        if not self._pts or not self._bounds:
            return
        has_meta = len(self._pts[0]) >= 4 and self._cfg["mapflags"]
        loop = [self._norm(p[0], p[1]) for p in self._pts]
        loop.append(loop[0])
        # Unterste Schicht: eine durchgehende dunkle Linie ("Casing"). Sie erzeugt
        # zwischen eng benachbarten Streckenteilen einen klaren Trennstreifen.
        parts = [{"color": "#0b0b12", "width": 4.4, "points": loop}]

        if not has_meta:
            parts.append({"color": SECTOR_COLORS[0], "width": 2.0, "points": loop})
        else:
            run = [self._pts[0]]
            def flush(bridge=None):
                seg = run + ([bridge] if bridge else [])
                if len(seg) > 1:
                    sec = run[0][3] if len(run[0]) > 3 else 0
                    parts.append({
                        "color": SECTOR_COLORS.get(sec, SECTOR_COLORS[0]),
                        "width": 2.0,
                        "points": [self._norm(p[0], p[1]) for p in seg],
                    })
            for p in self._pts[1:]:
                if (p[3] if len(p) > 3 else 0) == (run[0][3] if len(run[0]) > 3 else 0):
                    run.append(p)
                else:
                    flush(p)
                    run = [p]
            flush(self._pts[0])            # Schleife schliessen
        self._contour = parts
        self.contourChanged.emit()

    def _build_flags(self, zones) -> None:
        usable = (self._cfg["mapflags"] and zones and self._pts
                  and len(self._pts[0]) >= 3 and self._pts[0][2] >= 0 and self._bounds)
        if not usable:
            if self._flags:
                self._flags = []
                self.contourChanged.emit()
            self._flag_sig = ""
            return
        sig = ",".join(str(z.get("f")) for z in zones) + f"|{self._tf_sig}|{self._ver}"
        if sig == self._flag_sig:
            return
        self._flag_sig = sig

        def in_zone(frac, a, b):
            return (a <= frac < b) if a <= b else (frac >= a or frac < b)

        out = []
        for i, z in enumerate(zones):
            if z.get("f") != 3:            # nur gelbe Flaggen
                continue
            b = zones[i + 1]["s"] if i + 1 < len(zones) else zones[0]["s"]
            run = []
            for p in self._pts:
                # ⚠ frac == -1 (gelernt, bevor die Streckenlaenge bekannt war) darf
                # NICHT flaggen: bei einer ueber Start/Ziel laufenden Zone waere
                # sonst `-1 < b` fuer jeden Punkt wahr und die ganze Kontur gelb.
                if 0 <= p[2] <= 1 and in_zone(p[2], z["s"], b):
                    run.append(self._norm(p[0], p[1]))
                else:
                    if len(run) > 1:
                        out.append({"points": run})
                    run = []
            if len(run) > 1:
                out.append({"points": run})
        self._flags = out
        self.contourChanged.emit()

    # ── Autos ────────────────────────────────────────────────────────────────
    def update(self, drivers, focus_index, connected: bool, session: dict,
               enabled: bool, now_ms: float) -> None:
        if not enabled or not connected or not self._bounds or not self._done:
            self.isVisible = False
            return
        self.isVisible = True
        self.size = int(self._cfg["mapsize"] or 400)
        if self._ensure_transform():
            self._update_fit()          # gedrehte Kontur neu einpassen
            self._build_contour()
            self._flag_sig = ""
        self._build_flags((session or {}).get("marshal_zones"))

        dot = self._cfg["dotsize"] or 1
        show_numbers = bool(self._cfg["mapnumbers"])

        # Ausgeschiedene: on track aufgegeben -> Punkt bleibt kurz verblassend stehen.
        # In der Box aufgegeben -> gar kein Punkt.
        for d in drivers:
            if d.get("pos_xz"):
                self._last_pos[d["index"]] = d["pos_xz"]
            retired = d.get("dnf") or d.get("dsq")
            last = self._last_pos.get(d["index"])
            if retired and d.get("in_pit"):
                self._ghost.pop(d["index"], None)
            elif retired and last:
                if d["index"] not in self._ghost and d["index"] not in self._ghost_done:
                    self._ghost[d["index"]] = {
                        "t": now_ms, "x": last[0], "z": last[1],
                        "color": TEAM_COLORS.get(d.get("team") or "", "#767680")}
            elif not retired:
                self._ghost.pop(d["index"], None)
                self._ghost_done.discard(d["index"])

        cars = []
        for d in drivers:
            if not d.get("pos_xz") or d.get("dnf") or d.get("dsq") or d.get("in_pit"):
                continue
            if session.get("is_quali") and (d.get("driver_status") or 0) == 0:
                continue                                  # Garage -> nicht auf die Karte
            wx, wz = d["pos_xz"][0], d["pos_xz"][1]
            rec = self._move.get(d["index"])
            if rec is None:
                self._move[d["index"]] = [wx, wz, now_ms]
            elif math.hypot(wx - rec[0], wz - rec[1]) > self.STOPPED_M:
                self._move[d["index"]] = [wx, wz, now_ms]
            elif now_ms - rec[2] > self.STOPPED_MS:
                continue                                  # steht (geparkt/Crash) -> weg
            p = self._norm(wx, wz)
            if p is None:
                continue
            focused = d["index"] == focus_index
            cars.append({
                "key": d["index"], "x": p.x(), "y": p.y(),
                "r": (3.4 if focused else 2.9) * dot,
                "color": TEAM_COLORS.get(d.get("team") or "", "#ffffff"),
                "focused": focused, "retired": False, "opacity": 1.0,
                "label": str(d.get("position") or "") if show_numbers else "",
            })

        for idx in list(self._ghost):
            gh = self._ghost[idx]
            age = now_ms - gh["t"]
            if age >= DNF_LINGER_MS:
                del self._ghost[idx]
                self._ghost_done.add(idx)
                continue
            p = self._norm(gh["x"], gh["z"])
            if p is None:
                continue
            cars.append({
                "key": idx, "x": p.x(), "y": p.y(), "r": 2.9 * dot,
                "color": gh["color"], "focused": False, "retired": True,
                "opacity": 1.0 - age / DNF_LINGER_MS, "label": "",
            })

        if cars != self._cars:
            self._cars = cars
            self.carsChanged.emit()


def _smooth_loop(pts, passes: int = 2):
    """Kontur glaetten (smoothLoop in trackmap.js).

    Nur x und z werden gemittelt - Rundenanteil und Sektor bleiben pro Punkt
    erhalten, sonst verschieben sich Sektorgrenzen und Flaggen-Zuordnung.
    """
    if not pts or len(pts) < 8:
        return [list(p) for p in pts]
    out = [list(p) for p in pts]
    n = len(out)
    for _ in range(passes):
        src = [list(p) for p in out]
        for i in range(n):
            a, c = src[(i - 1) % n], src[(i + 1) % n]
            out[i][0] = a[0] * 0.25 + src[i][0] * 0.5 + c[0] * 0.25
            out[i][1] = a[1] * 0.25 + src[i][1] * 0.5 + c[1] * 0.25
    return out


# ── Verlaufs-Charts ──────────────────────────────────────────────────────────
class Charts(_StateBase):
    """Positions-, Gap- und Rundenzeiten-Verlauf (charts.js).

    Wird ueber /regie eingeschaltet. Die Positionen kommen vom Server, Gap und
    Rundenzeiten aus der Historie, die derive.py mitschreibt.
    """

    REDRAW_MS = 3000

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    titleChanged, title = _prop("title", str, "")
    modeChanged, mode = _prop("mode", str, "")
    maxLapChanged, maxLap = _prop("maxLap", int, 0)
    maxValueChanged, maxValue = _prop("maxValue", float, 0.0)
    zeroBasedChanged, zeroBased = _prop("zeroBased", bool, False)
    emptyTextChanged, emptyText = _prop("emptyText", str, "")
    seriesChanged = Signal()

    TITLES = {
        "gap": "Gap-Verlauf (zum Führenden)",
        "lap": "Rundenzeiten-Rückstand (zur schnellsten Runde)",
        "pos": "Positionsverlauf",
    }

    def __init__(self, base_url: str, shared, parent=None):
        super().__init__(parent)
        self._shared = shared
        self._fetch = _Fetcher(base_url, self)
        self._series = []
        self._positions = None       # letzte Antwort von /api/lap_positions
        self._pending = 0.0

    def set_base_url(self, url: str) -> None:
        self._fetch.set_base_url(url)

    @Property("QVariantList", notify=seriesChanged)
    def series(self):
        """[{name, color, points:[{lap, value}], last}] - Rohwerte, kein Pixel."""
        return self._series

    def update(self, mode: str, now_ms: float) -> None:
        if not mode:
            self.isVisible = False
            return
        self.isVisible = True
        if mode != self.mode:
            self.mode = mode
            self._pending = 0.0          # sofort neu zeichnen
        self.title = self.TITLES.get(mode, "Positionsverlauf")
        if now_ms - self._pending < self.REDRAW_MS:
            return
        self._pending = now_ms
        if mode == "pos":
            self._fetch.get("/api/lap_positions", self._on_positions)
            if self._positions is None:
                self.emptyText = "Noch keine Rundendaten"
                return
            self._build(self._positions.get("laps") or {},
                        self._positions.get("drivers") or {}, zero_based=False)
        elif mode == "gap":
            self._build(self._shared.gap_hist, self._meta(), zero_based=True)
        else:
            self._build(self._lap_deltas(), self._meta(), zero_based=True)

    def _on_positions(self, data: dict) -> None:
        self._positions = data
        if self.mode == "pos":
            self._build(data.get("laps") or {}, data.get("drivers") or {},
                        zero_based=False)

    def _meta(self):
        return {str(d["index"]): {"name": d.get("name") or "", "team": d.get("team") or ""}
                for d in self._shared.last_drivers}

    def _lap_deltas(self):
        """Rundenzeiten als Rueckstand auf die schnellste Runde im Datensatz."""
        best = math.inf
        for lap in self._shared.lap_hist.values():
            for v in lap.values():
                if 0 < v < best:
                    best = v
        if best is math.inf:
            return {}
        return {L: {k: round((v - best) * 100) / 100 for k, v in lap.items() if v > 0}
                for L, lap in self._shared.lap_hist.items()}

    @staticmethod
    def _normalize(laps_obj):
        """{Runde(int): {Fahrerindex(str): Wert}}.

        Noetig, weil die Quellen unterschiedlich schluesseln: gap_hist/lap_hist
        kommen aus Python mit int-Schluesseln, /api/lap_positions durch JSON mit
        Zeichenketten.
        """
        out = {}
        for lap, row in (laps_obj or {}).items():
            out[int(lap)] = {str(k): float(v) for k, v in row.items() if v is not None}
        return out

    def _build(self, laps_obj, meta, zero_based: bool) -> None:
        data = self._normalize(laps_obj)
        laps = sorted(data)
        if not laps:
            self._assign({"emptyText": "Noch keine Rundendaten", "maxLap": 0})
            if self._series:
                self._series = []
                self.seriesChanged.emit()
            return
        self.emptyText = ""
        max_lap = laps[-1]
        max_y = max((v for L in laps for v in data[L].values()), default=0.0)
        if zero_based:
            max_y = max(1.0, math.ceil(max_y / 5) * 5)

        idxs = set()
        for L in laps:
            idxs.update(data[L].keys())

        series = []
        for idx in sorted(idxs, key=lambda k: int(k) if k.isdigit() else 0):
            info = meta.get(idx) or {"name": f"#{idx}", "team": ""}
            points, last = [], None
            for L in laps:
                v = data[L].get(idx)
                if v is None:
                    continue
                points.append({"lap": L, "value": v})
                last = v
            if not points:
                continue
            series.append({
                "name": info.get("name") or "",
                "color": TEAM_COLORS.get(info.get("team") or "", "#ffffff"),
                "points": points, "last": last,
            })

        self._assign({"maxLap": max_lap, "maxValue": float(max_y),
                      "zeroBased": zero_based})
        self._series = series
        self.seriesChanged.emit()


# ── WM-Stand ─────────────────────────────────────────────────────────────────
class Championship(_StateBase):
    """WM-Stand aus championship.json plus Live-Hochrechnung (champ.js).

    Ein MANUELLES Panel: es erscheint nur, wenn es in /regie eingeschaltet ist.
    """

    POLL_MS = 3000

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    titleChanged, title = _prop("title", str, "")
    liveChanged, live = _prop("live", bool, False)
    rowsChanged = Signal()

    def __init__(self, base_url: str, parent=None):
        super().__init__(parent)
        self._fetch = _Fetcher(base_url, self)
        self._rows = []
        self._last = 0.0
        self._demo = None

    def set_base_url(self, url: str) -> None:
        self._fetch.set_base_url(url)

    @Property("QVariantList", notify=rowsChanged)
    def rows(self):
        return self._rows

    def set_demo_data(self, data) -> None:
        """Feste Tabelle statt Serverabfrage - fuer den Demo-Modus."""
        self._demo = data

    def update(self, wanted: bool, now_ms: float) -> None:
        if not wanted:
            if self.isVisible:
                self.isVisible = False
                self._rows = []
                self._last = 0.0
                self.rowsChanged.emit()
            return
        if getattr(self, "_demo", None) is not None:
            self._on_data(self._demo)
            return
        # Das Backend rechnet live (Basis plus Rennposition) -> regelmaessig neu holen.
        if now_ms - self._last > self.POLL_MS:
            self._last = now_ms
            self._fetch.get("/api/championship", self._on_data)

    def _on_data(self, data: dict) -> None:
        standings = data.get("standings")
        if not isinstance(standings, list):
            return
        rows = [{
            "pos": i + 1,
            "name": s.get("name") or "",
            "color": TEAM_COLORS.get(s.get("team") or "", "#ffffff"),
            "points": s.get("total_points") or 0,
            "livePoints": s.get("live_points") or 0,
        } for i, s in enumerate(standings[:12])]
        self._assign({"title": data.get("title") or "Championship",
                      "live": any(r["livePoints"] > 0 for r in rows),
                      "isVisible": True})
        if rows != self._rows:
            self._rows = rows
            self.rowsChanged.emit()
