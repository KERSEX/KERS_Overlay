"""
Ein erfundenes Rennen - fuers Bauen am Overlay ohne laufendes Spiel.

Warum das dazugehoert: am Overlay arbeitet man an Dingen, die im echten Rennen
selten oder nur einmal passieren - eine Ueberholung, ein Boxenstopp, die rote
Flagge, das Safety Car, der Wechsel von der Quali ins Rennen. Darauf zu warten,
waehrend man eine Schriftgroesse geradezieht, ist keine Arbeitsweise.

Der Demo-Feed erzeugt dieselben Payloads wie /api/stream, nur eben ausgedacht:
20 Fahrer, die sich gegenseitig ueberholen, Abstaende, die wandern, Reifen, die
altern, dazu Boxenstopps, Schaden, Strafen und im Minutentakt Safety Car, VSC und
rote Flagge. Der Rest des Overlays merkt keinen Unterschied - er bekommt einen
dict und weiss nicht, woher er kommt.

    kers_hud.py --demo          statt am Server zu haengen
"""

import json
import math
import random
import sys
import time
from pathlib import Path

from PySide6.QtCore import QObject, QTimer, Signal

from common import DATA_DIR

# Der Ordner mit overlay_settings.json: als EXE der Datenordner neben der EXE,
# im Dev-Betrieb eine Ebene ueber hud/ (dort schreibt main.py die Datei).
if getattr(sys, "frozen", False):
    ROOT = DATA_DIR
else:
    ROOT = Path(__file__).resolve().parent.parent


def _real_settings() -> dict:
    """Die echten Settings vom Server mitlesen, wenn es sie gibt.

    Ohne das zeigt der Demo-Modus immer die Vorgabewerte - man koennte also weder
    sein Branding noch die eingestellte Deckkraft oder Skalierung vorschauen. Die
    Datei ist dieselbe, die main.py schreibt; gibt es sie nicht (frisches Setup,
    fremder Rechner), bleibt es bei den Vorgaben.
    """
    try:
        with open(ROOT / "overlay_settings.json", encoding="utf-8") as f:
            return json.load(f)
    except Exception:  # pylint: disable=broad-exception-caught
        return {}

def _build_track(n: int = 600):
    """Eine erfundene, geschlossene Streckenkontur im Format von /api/track.

    Punkte sind [x, z, Rundenanteil, Sektor] - genau wie der Server sie liefert,
    nachdem er die Strecke gelernt hat. Die Form kommt aus einer Ueberlagerung
    zweier Kreisbewegungen: das ergibt eine unregelmaessige Schleife mit ein paar
    Kurven statt eines langweiligen Ovals.
    """
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        x = 620 * math.cos(a) + 140 * math.cos(3 * a)
        z = 430 * math.sin(a) + 160 * math.sin(2 * a)
        frac = i / n
        sector = 0 if frac < 1 / 3 else 1 if frac < 2 / 3 else 2
        pts.append([x, z, round(frac, 4), sector])
    return pts


def _track_point(track, frac: float):
    """Weltposition zum Rundenanteil - fuer die Punkte auf der Trackmap."""
    i = int(max(0.0, min(0.9999, frac)) * len(track))
    return [track[i][0], track[i][1]]


TEAMS = [
    ("Red Bull", ["VERSTAPPEN", "TSUNODA"]),
    ("McLaren", ["NORRIS", "PIASTRI"]),
    ("Ferrari", ["LECLERC", "HAMILTON"]),
    ("Mercedes", ["RUSSELL", "ANTONELLI"]),
    ("Aston Martin", ["ALONSO", "STROLL"]),
    ("Alpine", ["GASLY", "COLAPINTO"]),
    ("Williams", ["ALBON", "SAINZ"]),
    ("RB", ["HADJAR", "LAWSON"]),
    ("Audi", ["HULKENBERG", "BORTOLETO"]),
    ("Haas", ["OCON", "BEARMAN"]),
    # 11 Teams = 22 Autos, wie das Feld ab 2026. Passt ausserdem zu FULL_GRID im
    # Tower, der die Hoehe fuer 22 Zeilen reserviert.
    ("Cadillac", ["PEREZ", "BOTTAS"]),
]

COMPOUNDS = ["S", "M", "H"]


class DemoFeed(QObject):
    """Sendet dieselben Signale wie LiveFeed - austauschbar gegen das Original."""

    payload = Signal(dict)
    linkChanged = Signal(bool)

    def __init__(self, hz: int = 30, quali: bool = False, vorschau: bool = False,
                 parent=None):
        super().__init__(parent)
        self._quali = quali
        # vorschau = wir laufen nur, damit sich das Layout bearbeiten laesst.
        # Dann darf nichts bildschirmfuellend dazwischenfahren (siehe _emit).
        self._vorschau = bool(vorschau)
        self._t0 = time.monotonic()
        self._timer = QTimer(self)
        self._timer.setInterval(max(16, int(1000 / max(1, min(60, hz)))))
        self._timer.timeout.connect(self._emit)

        self._drivers = []
        for i, (team, names) in enumerate(TEAMS):
            for k, name in enumerate(names):
                idx = i * 2 + k
                self._drivers.append({
                    "index": idx, "name": name, "team": team,
                    # Grundtempo: vorne minimal schneller, plus etwas Zufall, damit
                    # die Reihenfolge nicht jeden Durchlauf identisch ist.
                    "pace": 1.0 + idx * 0.0016 + random.uniform(-0.0007, 0.0007),
                    "progress": float(len(TEAMS) * 2 - idx) * 0.4,
                    "compound": COMPOUNDS[idx % 3],
                    "tyre_age": random.randint(2, 18),
                    "grid": idx + 1,
                    "pit_until": 0.0, "stops": 0,
                    "dmg": [0, 0, 0], "penalty": 0, "warnings": 0,
                    "best": 0.0, "last": 0.0,
                })
        random.shuffle(self._drivers)
        self._track = _build_track()
        self._settings = _real_settings()
        self._rc_id = 0
        self._rc_next = 12.0        # erste Rennleitungs-Meldung nach 12 s

    def championship(self):
        """Ein erfundener WM-Stand - Format wie /api/championship.

        Gleicher Grund wie bei track_points(): ohne Server gibt es den Endpunkt
        nicht, das Panel bliebe leer.
        """
        rows = []
        for i, d in enumerate(sorted(self._drivers, key=lambda x: x["pace"])):
            rows.append({"name": d["name"], "team": d["team"],
                         "total_points": 310 - i * 17,
                         "live_points": max(0, 25 - i * 3)})
        return {"title": "Fahrerwertung", "standings": rows}

    def track_points(self):
        """Die erfundene Streckenkontur - Format wie /api/track: [x, z, frac, sektor].

        Der Demo-Feed hat keinen Server, also kann die Trackmap ihre Kontur nicht
        abholen. Die Bruecke reicht sie stattdessen direkt weiter.
        """
        return self._track

    # ── Steuerung (gleiche Schnittstelle wie LiveFeed) ───────────────────────
    def start(self) -> None:
        self._timer.start()
        self.linkChanged.emit(True)

    def stop(self) -> None:
        self._timer.stop()
        self.linkChanged.emit(False)

    def set_base_url(self, base_url: str) -> None:
        pass          # der Demo-Feed hat keinen Server

    def set_hz(self, hz: int) -> None:
        self._timer.setInterval(max(16, int(1000 / max(1, min(60, hz)))))

    # ── Rennen ───────────────────────────────────────────────────────────────
    def _emit(self) -> None:
        t = time.monotonic() - self._t0
        lap_time = 92.0
        lap = int(t / 8) + 1                       # eine "Runde" alle 8 Sekunden

        # Alle 70 s einmal durch die Zustaende: normal, VSC, Safety Car, rote Flagge.
        phase = int(t / 70) % 4
        sc_status = ["none", "vsc", "sc", "none"][phase]
        red_flag = phase == 3 and (t % 70) < 15

        # Startampel in den ersten Sekunden: die Lichter gehen nacheinander an,
        # bei 6 s alle aus.
        if t < 6.0:
            start_lights = {"num": min(5, int(t / 1.0)), "out": False, "age": 0.2}
        elif t < 9.0:
            start_lights = {"num": 0, "out": True, "age": t - 6.0}
        else:
            start_lights = {"num": 0, "out": False, "age": 9999}

        for d in self._drivers:
            if t < d["pit_until"]:
                continue
            d["progress"] += 0.016 * d["pace"] * (0.35 if sc_status != "none" else 1.0)
            # Ein bisschen Wellenbewegung, damit die Abstaende atmen und
            # gelegentlich echte Ueberholungen entstehen.
            d["progress"] += math.sin(t * 0.7 + d["index"]) * 0.0009

            # Boxenstopp: jeder Fahrer einmal um Runde 14 herum, gestaffelt.
            if d["stops"] == 0 and lap >= 12 + (d["index"] % 7):
                d["stops"] = 1
                d["pit_until"] = t + 3.0
                d["compound"] = COMPOUNDS[(COMPOUNDS.index(d["compound"]) + 1) % 3]
                d["tyre_age"] = 0
            else:
                d["tyre_age"] = min(45, int(lap - d["stops"] * 12) + 2)

            # Rundenzeit nur EINMAL pro Runde neu wuerfeln. Wuerde sie in jedem
            # Payload neu gezogen, verbesserte sich die Bestzeit staendig und der
            # Bestrunde-Flash im Tower liefe im Dauerbetrieb - im echten Rennen
            # passiert das einmal pro Runde und Fahrer.
            if d.get("last_lap_num") != lap:
                d["last_lap_num"] = lap
                d["last"] = lap_time / d["pace"] + random.uniform(-0.25, 0.25)
                if d["best"] == 0 or d["last"] < d["best"]:
                    d["best"] = d["last"]

        order = sorted(self._drivers, key=lambda x: -x["progress"])
        leader_prog = order[0]["progress"]
        fastest = min(order, key=lambda x: x["best"] or 9e9)

        out = []
        for pos, d in enumerate(order, start=1):
            behind = leader_prog - d["progress"]
            ahead = (order[pos - 2]["progress"] - d["progress"]) if pos > 1 else 0.0
            in_pit = t < d["pit_until"]
            s1 = 30.0 / d["pace"] + math.sin(t * 0.3 + d["index"]) * 0.15
            s2 = 31.5 / d["pace"] + math.cos(t * 0.25 + d["index"]) * 0.15
            out.append({
                "index": d["index"], "name": d["name"], "team": d["team"],
                "position": pos,
                "gap_to_leader": round(behind * 3.4, 3),
                "gap_to_ahead": round(ahead * 3.4, 3),
                "compound": d["compound"], "tyre_age": d["tyre_age"],
                "last_lap": round(d["last"], 3), "best_lap": round(d["best"], 3),
                "current_lap_time": round((t % 8) * 11.5, 3),
                "sector1": round(s1, 3), "sector2": round(s2, 3),
                "best_sectors": [round(s1 - 0.2, 3), round(s2 - 0.2, 3), 30.4],
                "drs": pos > 1 and ahead * 3.4 < 1.0,
                "overtake_active": pos in (2, 5) and int(t) % 12 < 3,
                "overtake_available": pos > 1 and ahead * 3.4 < 1.0,
                "ers_pct": 40 + (d["index"] * 7) % 60, "ers_mode": 1,
                # Telemetrie fuers Onboard: Tempo, Gang, Gas und Bremse schwingen
                # ueber die Runde, als wuerde der Wagen Kurven fahren.
                "speed": 0 if in_pit else int(
                    150 + 130 * abs(math.sin(d["progress"] * 6.3 + d["index"]))),
                "gear": 0 if in_pit else 3 + int(
                    4 * abs(math.sin(d["progress"] * 6.3 + d["index"]))),
                "throttle": 0.0 if in_pit else max(
                    0.0, math.sin(d["progress"] * 6.3 + d["index"])),
                "brake": 0.0 if in_pit else max(
                    0.0, -math.sin(d["progress"] * 6.3 + d["index"])) * 0.9,
                # Standzeit des letzten Stopps - erscheint auf der Pit-Karte.
                "pit_time": 2.4 + (d["index"] % 5) * 0.3 if d["stops"] else 0.0,
                "in_pit": in_pit, "dnf": d["index"] == 19 and lap > 6,
                "dsq": False, "driver_status": 1 if not in_pit else 0,
                "lap_invalid": self._quali and d["index"] % 9 == 3,
                # Zwei Fahrer sammeln Strafen ein, damit die Pillen links zu sehen sind.
                "penalties": 5 if d["index"] == 3 and lap > 4 else 0,
                "pen_dt": 1 if d["index"] == 8 and lap > 9 else 0,
                "corner_warnings": (int(t / 20) % 3) if d["index"] == 11 else 0,
                "lap_num": lap,
                "lap_distance": (d["progress"] % 1.0) * 5300,
                # Weltposition fuer die Trackmap: Punkt auf der erfundenen Kontur,
                # der zum Rundenanteil passt.
                "pos_xz": _track_point(self._track, d["progress"] % 1.0),
                "laps_down": 1 if d["index"] == 17 and lap > 20 else 0,
                "sector": min(2, int((t % 8) / 2.7)),
                "grid_position": d["grid"],
                # Schaden nur bei ein paar Autos - einer davon kritisch (blinkt).
                "dmg_fl": 35 if d["index"] == 5 else 0,
                "dmg_fr": 0,
                "dmg_rw": 72 if d["index"] == 2 and lap > 3 else 0,
                "finished": False,
                "pit_stops": d["stops"], "stints": [],
            })

        self.payload.emit({
            "drivers": out,
            "connected": True,
            "session": {
                "type": 5 if self._quali else 10,
                "type_name": "Q1" if self._quali else "Rennen",
                "is_quali": self._quali,
                "current_lap": lap, "total_laps": 58,
                "track_length": 5300, "track_id": 1,
                "formula": 13, "formula_name": "F1 26",
                "safety_car_status": sc_status,
                # Quali-Uhr laeuft in 220-s-Runden von 200 auf 0. Kurz genug, dass
                # man die Gefahrenzone (letzte 3 Minuten) auch wirklich zu sehen
                # bekommt, ohne eine echte Viertelstunde zu warten.
                "time_left": max(0, 200 - int(t % 220)) if self._quali else 0,
                "weather_name": "Leicht bewoelkt", "weather_emoji": "⛅",
                "weather_rain": 12,
                "forecast": [{"t": 5, "emoji": "⛅", "rain": 18},
                             {"t": 10, "emoji": "🌧️", "rain": 55},
                             {"t": 15, "emoji": "🌧️", "rain": 70},
                             {"t": 30, "emoji": "☀️", "rain": 8}],
                "fastest_lap_driver": fastest["name"],
                "fastest_lap_time": round(fastest["best"], 3),
                "is_spectating": False, "spectator_index": 255,
                # Zwei gelbe Abschnitte, solange Safety Car oder VSC laeuft.
                "marshal_zones": ([{"s": 0.0, "f": 0}, {"s": 0.18, "f": 3},
                                   {"s": 0.34, "f": 0}, {"s": 0.62, "f": 3},
                                   {"s": 0.78, "f": 0}]
                                  if sc_status != "none" else
                                  [{"s": 0.0, "f": 0}, {"s": 0.5, "f": 0}]),
            },
            "race_control": self._race_control(t, red_flag, order),
            # Kamerawechsel alle 20 s -> das Lower-Third blendet neu ein.
            "focus_index": order[int(t / 20) % min(6, len(order))]["index"],
            "start_lights": start_lights,
            "final_classification": [], "quali_results": {},
            # Erst die eigenen Vorgaben, darueber die echten Settings - so sieht die
            # Demo aus wie das laufende Overlay, inklusive Branding und Deckkraft.
            "settings": dict({"brand_title": "KERS", "tower": True, "ticker": True,
                              "damage": True, "comeback": True, "pbflash": True,
                              "opacity": 1.0, "scale": 0, "rows": 0},
                             **self._settings),
            "udp": {"got": 2026, "want": 2026},
            # Die Regie blendet im Demo-Modus von selbst durch: so bekommt man auch
            # Chart und WM-Stand zu sehen, ohne /regie zu bedienen (die es hier
            # ohne Server ohnehin nicht gibt).
            # ⚠ Im Vorschau-Modus (Layout bearbeiten) KEIN Chart: das legt sich
            # bildschirmfuellend ueber alles und man kaeme an keinen Baustein mehr
            # heran. Der WM-Stand dagegen laeuft dort dauerhaft mit, damit er sich
            # ueberhaupt platzieren laesst - sonst waere er nur 25 von 140
            # Sekunden zu sehen.
            "regie": {"chart": "none" if self._vorschau else self._demo_chart(t),
                      "champ": True if self._vorschau else (100 <= (t % 140) < 125),
                      "battles": True, "hotlap": True},
        })

    @staticmethod
    def _demo_chart(t: float) -> str:
        """Nur "gap" und "lap".

        Der Positionsverlauf holt seine Daten von /api/lap_positions - ohne Server
        gibt es die nicht, das Chart bliebe leer. Gap- und Rundenzeiten-Verlauf
        schreibt derive.py dagegen selbst mit und funktionieren auch hier.
        """
        cycle = t % 140
        if 55 <= cycle < 70:
            return "gap"
        if 70 <= cycle < 82:
            return "lap"
        return "none"

    def _race_control(self, t: float, red_flag: bool, order):
        """Ab und zu eine Meldung, damit der Banner etwas zu tun hat.

        Der Server zaehlt die Ids hoch und schickt immer den ganzen Feed mit; die
        Empfaenger merken sich die zuletzt gesehene Id. Hier genauso.
        """
        if red_flag and self._rc_id % 7 != 6:
            self._rc_id += 1
            return [{"id": self._rc_id, "type": "redflag", "text": "ROTE FLAGGE"}]
        if t < self._rc_next:
            return []
        self._rc_next = t + 9.0
        self._rc_id += 1
        who = order[self._rc_id % min(8, len(order))]["name"]
        kind, text = [
            ("tracklimit", f"TRACK LIMITS · {who}"),
            ("penalty", f"5 SEKUNDEN STRAFE · {who}"),
            ("mom", f"OVERTAKE MODE · {who}"),
            ("penserved", f"STRAFE ABGELEISTET · {who}"),
            ("flag", "GRUENE FLAGGE · SEKTOR 2"),
        ][self._rc_id % 5]
        return [{"id": self._rc_id, "type": kind, "text": text}]
