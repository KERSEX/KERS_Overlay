"""
Die gemeinsame Buchhaltung des Overlays - Portierung von static/js/core.js.

WARUM DAS HIER LIEGT UND NICHT IN QML
-------------------------------------
Im Web-Overlay macht core.js zwei Dinge: es haengt am SSE-Strom UND es fuehrt den
abgeleiteten Zustand, den mehrere Bausteine gemeinsam brauchen (Bestsektoren,
Stint-Laengen, Gap-Verlauf, Safety-Car-Fenster, das eingefrorene Quali-Ergebnis).
Der Kommentar oben in core.js erklaert, warum es das ueberhaupt gibt: die grosse
Tower-Schleife pflegte diesen Zustand frueher nebenbei, und die Einzelseiten hatten
ihn deshalb nicht.

Fuer QML gilt dasselbe Argument noch einmal verschaerft. Diese Buchhaltung ist reine
Rechnerei ueber Zahlenreihen - in Python ist sie lesbar, testbar und laeuft einmal
pro Payload, statt in QML-JavaScript in jedem Delegate erneut. QML bekommt dadurch
fertige Werte und bleibt reine Darstellung.

⚠ Diese Datei ist eine ABSICHTLICH TREUE Portierung. Wo core.js/tower.js eine
Eigenheit haben (Reihenfolge von Einfaerben und Verrechnen, Toleranzen, die
Sonderbehandlung von S3 an der Start/Ziel-Linie), steht sie hier genauso drin -
sonst sieht das QML-Overlay anders aus als das Web-Overlay, das daneben laeuft.
Aenderungen deshalb bitte immer in beiden Dateien.

Herkunft der Bloecke:
    Config            core.js  DEFAULT_CFG / applySettings
    SharedState       core.js  deriveShared / maybeHoldQuali / qualiStatus /
                               mergeBestSectors / updateRaceBest / absorbSectors /
                               trackStints
    _sector_view      tower.js colorSectors / showBestSectors / clearSectorColors
"""

import math
import time

INF = math.inf

# ── Konfiguration ────────────────────────────────────────────────────────────
# 1:1 aus DEFAULT_CFG in core.js. Der Server schickt seine Settings mit jedem
# Payload mit; was er nicht kennt, bleibt auf dem Vorgabewert.
DEFAULT_CFG = {
    "tower": True, "battles": True, "map": True, "onboard": True, "lights": True,
    "damage": True, "msgs": True, "ticker": True, "lowerthird": True, "flbanner": True,
    "undercut": True, "deltabar": True, "mapnumbers": True, "mapflags": True,
    "pred": True, "danger": True, "comeback": True, "pbflash": True, "fresh": True,
    "strat": True, "pitproj": True,
    "brand_title": "", "brand_accent": "#e10600", "header_color": "", "row_color": "",
    "tower_logo": "", "tower_logo_pos": "left", "tower_logo_h": 34,
    "scale": 0, "rows": 0, "mapsize": 400, "maprot": 110, "mapflip": True, "dotsize": 1.0,
    "holds": 300, "ltdur": 4.0, "flbdur": 4.5, "dmgcrit": 60, "battlethresh": 1.5,
    "preset": "voll",
    "ampel": True,
    "mapcorner": "tr",
    "opacity": 1.0, "text_outline": 0.0,
    # ⚠ Ohne Eintrag HIER kaeme die Einstellung nie an: Config.apply uebernimmt
    # nur Schluessel, die in DEFAULT_CFG stehen.
    "battleboxes": 4,
    "hotlapboxes": 4,
    "battledir": "row",
    "penside": "left",
    "penhidefinish": True,
    "podiumstil": "flat",
    "layout": {},
}


class Config:
    """Die Settings des Servers, gemischt mit lokalen Ueberschreibungen.

    Im Web-Overlay uebernehmen URL-Parameter diese Rolle (`?rows=10&scale=0.8`).
    Hier ist es ein Dictionary, das das HUD setzen kann - gleiche Regel: was lokal
    gesetzt ist, gewinnt gegen den Server.
    """

    def __init__(self, overrides: dict | None = None):
        self._override = dict(overrides or {})
        self._values = dict(DEFAULT_CFG)

    def apply(self, settings: dict | None) -> None:
        for key, default in DEFAULT_CFG.items():
            if key in self._override:
                self._values[key] = self._override[key]
            elif settings and settings.get(key) is not None:
                self._values[key] = settings[key]
            else:
                self._values[key] = default

    def set_override(self, key: str, value) -> None:
        """Lokal ueberschreiben. `None` gibt den Wert wieder an den Server zurueck."""
        if value is None:
            self._override.pop(key, None)
        else:
            self._override[key] = value

    def __getitem__(self, key):
        return self._values.get(key, DEFAULT_CFG.get(key))

    def get(self, key, default=None):
        return self._values.get(key, default)

    def as_dict(self) -> dict:
        return dict(self._values)


# ── Hilfsgroessen ────────────────────────────────────────────────────────────
def _now_ms() -> float:
    """Gegenstueck zu performance.now() - monoton, unabhaengig von der Systemuhr."""
    return time.monotonic() * 1000.0


def _s3_live(d: dict) -> float:
    """S3 der laufenden Runde, sofern die Runde schon abgeschlossen ist.

    Genau wie in core.js: aus last_lap minus S1 minus S2. Vor dem Rundenende gibt es
    keinen S3 - dann 0.
    """
    if d.get("last_lap", 0) > 0 and d.get("sector1", 0) > 0 and d.get("sector2", 0) > 0:
        return d["last_lap"] - d["sector1"] - d["sector2"]
    return 0.0


class SharedState:
    """Alles, was mehrere Bausteine gemeinsam lesen. Ein Objekt pro Overlay."""

    TOL = 1.4          # Pace-Toleranz in qualiStatus (core.js)
    SEC_EPS = 0.005    # Toleranz beim Sektor-Vergleich (tower.js colorSectors)

    def __init__(self, cfg: Config):
        self.cfg = cfg

        # Sektor-Buchhaltung
        self.sec_best = [INF, INF, INF]     # Session-Bestzeit je Sektor
        self.drv_sec_best = {}              # idx -> [s1,s2,s3] persoenliche Bestsektoren
        self.best_lap_secs = {}             # idx -> Sektoren der SCHNELLSTEN Runde
        self.prev_last_lap = {}             # idx -> zuletzt gesehene Rundenzeit
        self.last_quali_secs = {}           # idx -> [s1,s2] der laufenden Runde
        self.cur_s3 = {}                    # idx -> S3 der zuletzt beendeten Runde
        self._last_sec_type = None          # Session-Typ -> Wechsel setzt alles zurueck

        # Stints (Lower-Third)
        self.stint_len = {}
        self._prev_comp = {}
        self._prev_age = {}

        # Safety-Car-Fenster (Onboard)
        self.sc_onboard_active = False
        self.sc_restart_until_lap = 0
        self._last_sc_status = None

        # Verlaeufe (Charts)
        self.gap_hist = {}
        self.lap_hist = {}
        self._gap_last_lap = 0

        # Eingefrorenes Quali-Ergebnis (maybeHoldQuali)
        self._q_snap = None
        self._q_live_drivers = None
        self._q_live_session = None
        self._q_was_quali = False
        self._q_since = 0.0

        self.last_drivers = []
        self.last_lap_count = 0

    # ── Ergebnis stehen lassen (Quali -> Rennen) ─────────────────────────────
    def maybe_hold_quali(self, data: dict) -> dict:
        """Nach der Quali das Ergebnis noch `holds` Sekunden stehen lassen.

        Portierung von maybeHoldQuali in core.js. Ohne das waere direkt nach dem
        Ende der Quali eine leere Tabelle zu sehen, weil das Spiel schon auf die
        naechste Session umschaltet.
        """
        session = data.get("session") or {}
        now = _now_ms()

        if data.get("connected") and session.get("is_quali") and data.get("drivers"):
            self._q_live_drivers = data["drivers"]
            self._q_live_session = session
            self._q_was_quali = True
            self._q_snap = None
            return data

        if self._q_was_quali and not session.get("is_quali") \
                and self._q_snap is None and self._q_live_drivers:
            self._q_snap = self._q_live_drivers
            self._q_since = now

        if self._q_snap is None:
            return data

        sl = data.get("start_lights") or {}
        lights = (sl.get("num", 0) > 0 or sl.get("out")) and sl.get("age", 9999) < 30
        racing = data.get("connected") and not session.get("is_quali") \
            and (session.get("current_lap") or 0) >= 1
        expired = (now - self._q_since) > (self.cfg["holds"] or 300) * 1000

        if lights or racing or expired:
            self._q_snap = None
            self._q_was_quali = False
            return data

        held_session = dict(self._q_live_session or {})
        held_session.update({"_quali_result": True, "safety_car_status": "none",
                             "time_left": 0})
        out = dict(data)
        out.update({"drivers": self._q_snap, "connected": True, "focus_index": -1,
                    "race_control": [], "session": held_session})
        return out

    # ── Quali-Status eines Fahrers ───────────────────────────────────────────
    def quali_status(self, d: dict) -> str:
        """box / out / inlap / track(=Hotlap). Portierung von qualiStatus in core.js.

        Die vier nummerierten Faelle im Original stehen hier als dieselben Kommentare -
        die Logik ist ueber Monate an echten Sessions geradegezogen worden.
        """
        ds = d.get("driver_status", 0)
        if d.get("in_pit"):
            st = "box"
        elif ds == 3:
            st = "out"
        elif ds == 2:
            st = "inlap"
        elif ds in (1, 4):
            st = "track"
        else:
            st = "box"
        if st != "track":
            return st

        # (1) Ungueltige Runde ist nie ein Hotlap.
        if d.get("lap_invalid"):
            return "inlap"

        pb = self.drv_sec_best.get(d["index"])
        clt = d.get("current_lap_time") or 0

        too_slow = False   # klar langsamer als die Bestpace -> Runde hin
        on_pace = False    # fertiger Sektor nahe der Bestpace -> bewiesen schnell
        if pb:
            s3 = _s3_live(d)
            ab, have = 0.0, False
            for i, v in enumerate((d.get("sector1", 0), d.get("sector2", 0), s3)):
                if v > 0 and pb[i] < INF:
                    ab += v - pb[i]
                    have = True
            if have:
                if ab > self.TOL:
                    too_slow = True
                else:
                    on_pace = True
            # LIVE, schon MITTEN im Sektor ueber der Bestpace bis zum Sektorende.
            if clt > 0.5:
                cum, ok = 0.0, True
                for i in range(0, min(d.get("sector", 0), 2) + 1):
                    if pb[i] < INF:
                        cum += pb[i]
                    else:
                        ok = False
                if ok and clt > cum + self.TOL:
                    too_slow = True

        if too_slow:
            return "inlap"                                  # (2) klar zu langsam
        if on_pace:
            return "track"                                  # (3) bewiesen schnell
        # (4) Frischer Start ohne Sektor-Beweis: fliegende Runde / am Deployen / genug ERS.
        if ds == 1 or (d.get("ers_mode") or 0) >= 2 or (d.get("ers_pct") or 0) >= 40:
            return "track"
        return "out"

    # ── Sektor-Buchhaltung ───────────────────────────────────────────────────
    def _pb(self, idx: int) -> list:
        return self.drv_sec_best.setdefault(idx, [INF, INF, INF])

    def merge_best_sectors(self, d: dict) -> None:
        """Bestsektoren vom SPIEL (Paket 11) uebernehmen.

        Ohne das kennt das Overlay nur Sektoren, die es selbst mitgehoert hat - alles
        vor dem Einschalten fehlt. Zusammengefuehrt per Minimum.
        """
        bs = d.get("best_sectors")
        if not bs:
            return
        pb = self._pb(d["index"])
        for i in range(3):
            v = bs[i] if i < len(bs) else 0
            if not v or v <= 0:
                continue
            if v < pb[i]:
                pb[i] = v
            if v < self.sec_best[i]:
                self.sec_best[i] = v

    def update_race_best(self, idx: int, secs) -> None:
        """Persoenliche Bestsektoren auch im Rennen fuehren (Basis fuer den Delta-Balken)."""
        pb = self._pb(idx)
        for i, v in enumerate(secs):
            if v > 0 and v < pb[i]:
                pb[i] = v

    def absorb_sectors(self, idx: int, secs) -> None:
        """Live gefahrene Sektoren in Session- und Fahrer-Bestzeit einrechnen."""
        pb = self._pb(idx)
        for i, v in enumerate(secs):
            if not v or v <= 0:
                continue
            if v < self.sec_best[i]:
                self.sec_best[i] = v
            if v < pb[i]:
                pb[i] = v

    def track_stints(self, drivers) -> None:
        """Stint-Laengen mitschreiben (Chips im Lower-Third)."""
        for d in drivers:
            comp = d.get("compound")
            if not comp or comp not in "SMHIWE":
                continue
            idx = d["index"]
            prev = self._prev_comp.get(idx)
            if prev is not None and prev != comp:
                lst = self.stint_len.setdefault(idx, [])
                lst.append({"c": prev, "laps": self._prev_age.get(idx, 0)})
                if len(lst) > 6:
                    lst.pop(0)
            self._prev_comp[idx] = comp
            self._prev_age[idx] = d.get("tyre_age") or 0

    def _reset_session(self) -> None:
        self.sec_best = [INF, INF, INF]
        for store in (self.drv_sec_best, self.best_lap_secs, self.prev_last_lap,
                      self.last_quali_secs, self.cur_s3, self.stint_len,
                      self._prev_comp, self._prev_age):
            store.clear()
        self.gap_hist = {}
        self.lap_hist = {}
        self._gap_last_lap = 0
        self.sc_restart_until_lap = 0
        self.sc_onboard_active = False

    # ── Hauptdurchlauf ───────────────────────────────────────────────────────
    def derive(self, data: dict):
        """Portierung von deriveShared(). Liefert die sortierte Fahrerliste zurueck.

        Der Rueckgabewert enthaelt DIESELBEN dicts wie data["drivers"] (keine Kopie) -
        die Sektor-Anzeige haengt sich spaeter mit eigenen Schluesseln daran.
        """
        session = data.get("session") or {}

        # Session-Wechsel -> alle Bestzeiten und Verlaeufe zuruecksetzen
        if session.get("type") != self._last_sec_type:
            self._last_sec_type = session.get("type")
            self._reset_session()

        # Safety-Car-Statuswechsel -> Onboard-Fenster
        sc_status = session.get("safety_car_status") if data.get("connected") else "none"
        sc_changed = None
        if data.get("connected") and sc_status != self._last_sc_status:
            if sc_status in ("sc", "vsc"):
                if not session.get("is_quali"):
                    self.sc_onboard_active = True
            elif sc_status == "none" and self._last_sc_status in ("sc", "vsc"):
                # Restart: Onboard auf P1, bis der Fuehrende ueber Start/Ziel ist
                p1 = next((x for x in (data.get("drivers") or [])
                           if x.get("position") == 1), None)
                if p1:
                    self.sc_restart_until_lap = (p1.get("lap_num") or 0) + 1
            sc_changed = (sc_status, self._last_sc_status)
            self._last_sc_status = sc_status

        # Nach Position sortieren - alle Bausteine sehen dieselbe Reihenfolge.
        drivers = sorted((data.get("drivers") or []),
                         key=lambda a: (a.get("position") or 999, a.get("index") or 0))
        if self.cfg["rows"] > 0:
            drivers = drivers[:int(self.cfg["rows"])]

        # Gap-/Rundenzeiten-Verlauf: pro neuer Runde ein Schnappschuss
        if not session.get("is_quali") and data.get("connected"):
            gl = session.get("current_lap") or 0
            if gl > 0 and gl != self._gap_last_lap:
                self._gap_last_lap = gl
                snap, lap_snap = {}, {}
                for d in drivers:
                    if d.get("dnf") or d.get("dsq"):
                        continue
                    snap[d["index"]] = round((d.get("gap_to_leader") or 0) * 100) / 100
                    if (d.get("last_lap") or 0) > 0:
                        lap_snap[d["index"]] = round(d["last_lap"] * 1000) / 1000
                self.gap_hist[gl] = snap
                self.lap_hist[gl] = lap_snap

        is_quali = bool(session.get("is_quali"))
        for d in drivers:
            self.merge_best_sectors(d)
            idx = d["index"]
            s3 = _s3_live(d)

            # Rundenende: beim Ueberfahren der Start/Ziel-Linie setzt das Spiel
            # sector1/sector2 der NEUEN Runde schon auf 0 -> fuer S3 der GERADE
            # beendeten Runde die zuletzt gesehenen S1/S2 nehmen.
            if is_quali and (d.get("last_lap") or 0) > 0 \
                    and self.prev_last_lap.get(idx) != d["last_lap"]:
                self.prev_last_lap[idx] = d["last_lap"]
                ls = self.last_quali_secs.get(idx)
                s1c = d["sector1"] if d.get("sector1", 0) > 0 else (ls[0] if ls else 0)
                s2c = d["sector2"] if d.get("sector2", 0) > 0 else (ls[1] if ls else 0)
                s3c = (d["last_lap"] - s1c - s2c) if (s1c > 0 and s2c > 0) else 0
                if s3c > 0:
                    self.cur_s3[idx] = s3c                    # fuers Onboard
                    pb = self._pb(idx)
                    if s3c < pb[2]:
                        pb[2] = s3c
                    if s3c < self.sec_best[2]:
                        self.sec_best[2] = s3c
                    if abs(d["last_lap"] - (d.get("best_lap") or 0)) < 0.005:
                        self.best_lap_secs[idx] = [s1c, s2c, s3c]

            # Zuletzt gueltige S1/S2 merken -> ueberlebt den Start/Ziel-Reset oben.
            if d.get("sector1", 0) > 0 and d.get("sector2", 0) > 0:
                self.last_quali_secs[idx] = [d["sector1"], d["sector2"]]

            if is_quali:
                # Wie im Tower: nur auf einem echten Hotlap fliessen die LIVE-Sektoren
                # in die Bestzeiten.
                if self.quali_status(d) == "track":
                    self.absorb_sectors(idx, [d.get("sector1", 0), d.get("sector2", 0), s3])
            else:
                self.update_race_best(idx, [d.get("sector1", 0), d.get("sector2", 0), s3])

        if not is_quali and data.get("connected"):
            self.track_stints(drivers)

        self.last_lap_count = session.get("current_lap") or self.last_lap_count
        self.last_drivers = drivers
        return drivers, sc_changed

    # ── Sektor-Anzeige (tower.js) ────────────────────────────────────────────
    def sector_view(self, d: dict, is_quali: bool, quali_st: str):
        """Welche drei Sektorzeiten stehen in der Zeile, und in welcher Farbe?

        Portierung von colorSectors / showBestSectors / clearSectorColors in tower.js.
        Rueckgabe: ([s1,s2,s3] in Sekunden oder 0, ["sp"|"sg"|"sy"|""] x3)

        ⚠ Reihenfolge beachten: im Web-Overlay hat deriveShared die Live-Sektoren
        SCHON in sec_best/pb eingerechnet, bevor colorSectors vergleicht. Ein neuer
        Bestsektor ist deshalb immer lila (v <= v + eps). derive() oben macht genau
        dasselbe, also stimmt das Ergebnis hier ueberein.
        """
        idx = d["index"]
        if is_quali and quali_st != "track":
            # Box / Outlap / Inlap -> die Sektoren der schnellsten Runde des Fahrers.
            # Fallback auf die Bestsektoren, solange keine komplette Runde erfasst ist.
            src = self.best_lap_secs.get(idx) or self.drv_sec_best.get(idx) \
                or [INF, INF, INF]
            times, classes = [], []
            for i in range(3):
                v = src[i]
                if not (v > 0 and v < INF):
                    times.append(0.0)
                    classes.append("")
                    continue
                times.append(v)
                classes.append("sp" if v <= self.sec_best[i] + self.SEC_EPS else "sg")
            return times, classes

        times = [d.get("sector1", 0) or 0, d.get("sector2", 0) or 0, _s3_live(d)]
        if not is_quali:
            return times, ["", "", ""]      # im Rennen ohne Einfaerbung

        pb = self._pb(idx)
        classes = []
        for i, v in enumerate(times):
            if not v or v <= 0:
                classes.append("")
                continue
            if v <= self.sec_best[i] + self.SEC_EPS:
                classes.append("sp")        # Session-Best
            elif v <= pb[i] + self.SEC_EPS:
                classes.append("sg")        # persoenliche Bestzeit
            else:
                classes.append("sy")        # langsamer
            if v < self.sec_best[i]:
                self.sec_best[i] = v
            if v < pb[i]:
                pb[i] = v
        return times, classes
