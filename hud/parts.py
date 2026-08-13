"""
Der abgeleitete Zustand der einzelnen Overlay-Bausteine.

Gegenstueck zu den JS-Dateien in static/parts/: dort rechnet jede Bausteinseite in
ihrem eigenen `KERS.onData`-Hook, hier tut es je eine Klasse. Die Rechnerei bleibt
1:1 dieselbe - Schwellen, Hysterese, Wartezeiten und Reihenfolgen sind aus dem
Original uebernommen, weil sie an echten Rennen geradegezogen wurden.

    MessageBanner    _msgbanner.js   Warteschlange fuer die Meldungen oben
    RaceControl      racemsg.js      Strafen, Flaggen, Safety Car -> Banner
    Undercut         undercut.js     Undercut-Erkennung -> derselbe Banner
    Battles          battles.js      Kampfgruppen + Ueberhol-Projektion
    Hotlaps          hotlap.js       wer faehrt gerade eine fliegende Runde
    Onboard          onboard.js      Telemetrie des Kamera-Fahrers
    LowerThird       lowerthird.js   Namens-Tag beim Kamerawechsel
    PitCards         pit.js          Live-Boxenstopp-Timer
    StartLights      lights.js       die fuenf Startlichter
    FastestLap       flbanner.js     das lila Bestrunden-Banner
    DangerZone       danger.js       Countdown auf dem letzten sicheren Platz
    PitProjection    pitproj.js      "wo kaeme er nach dem Stopp raus"

⚠ Wie bei derive.py gilt: aendert sich etwas am Web-Overlay, muss es hier mit.
"""

import time

from PySide6.QtCore import (QAbstractListModel, QByteArray, QModelIndex, QObject,
                            Property, QTimer, Qt, Signal, Slot)

from models import TEAM_COLORS, TEAM_LOGOS, TYRE_ICONS, _prop, _StateBase

INF = float("inf")

# Zeilenhoehe einer Battle-Zeile - identisch zu BATTLE_ROW_H in battles.js.
BATTLE_ROW_H = 44


def _now_ms() -> float:
    """Wanduhr in Millisekunden - passt zu Date.now() in QML."""
    return time.time() * 1000.0


def _team_color(team: str) -> str:
    return TEAM_COLORS.get(team, "#ffffff")


def _s3_of(d: dict) -> float:
    if d.get("last_lap", 0) > 0 and d.get("sector1", 0) > 0 and d.get("sector2", 0) > 0:
        return d["last_lap"] - d["sector1"] - d["sector2"]
    return 0.0


class SlotModel(QAbstractListModel):
    """Listenmodell, dessen Zeilen an ihrem Platz bleiben.

    Dieselbe Idee wie DriverModel in models.py: der Schluessel ist der Fahrerindex,
    die SICHTBARE Reihenfolge steht in der Rolle `slot`. QML setzt daraus die
    Y-Position und laesst eine Behavior-Animation gleiten, statt Delegates
    umzubauen. Siehe den ausfuehrlichen Kommentar dort.
    """

    countChanged = Signal()

    def __init__(self, roles, parent=None):
        super().__init__(parent)
        self._names = list(roles)
        self._roles = {Qt.ItemDataRole.UserRole + i: QByteArray(n.encode())
                       for i, n in enumerate(self._names)}
        self._role_of = {n: Qt.ItemDataRole.UserRole + i
                         for i, n in enumerate(self._names)}
        self._rows = []
        self._by_key = {}

    def roleNames(self):
        return self._roles

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._rows)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self._rows):
            return None
        name = self._roles.get(role)
        return self._rows[index.row()].get(bytes(name).decode()) if name else None

    @Property(int, notify=countChanged)
    def count(self):
        return len(self._rows)

    def sync(self, entries) -> None:
        """`entries` = Liste von (key, dict). Reihenfolge = Platz (`slot`)."""
        seen = set()
        for slot, (key, values) in enumerate(entries):
            seen.add(key)
            values = dict(values, slot=slot)
            row = self._by_key.get(key)
            if row is None:
                row = dict(values, _pos=len(self._rows))
                self.beginInsertRows(QModelIndex(), len(self._rows), len(self._rows))
                self._rows.append(row)
                self._by_key[key] = row
                self.endInsertRows()
                self.countChanged.emit()
                continue
            changed = [k for k, v in values.items() if row.get(k) != v]
            if changed:
                row.update(values)
                pos = self.index(row["_pos"], 0)
                self.dataChanged.emit(pos, pos, [self._role_of[c] for c in changed
                                                 if c in self._role_of])
        for key in [k for k in self._by_key if k not in seen]:
            self._remove(key)

    def _remove(self, key) -> None:
        row = self._by_key.pop(key, None)
        if row is None:
            return
        pos = row["_pos"]
        self.beginRemoveRows(QModelIndex(), pos, pos)
        self._rows.pop(pos)
        self.endRemoveRows()
        for later in self._rows[pos:]:
            later["_pos"] -= 1
        self.countChanged.emit()

    def clear(self) -> None:
        if not self._rows:
            return
        self.beginResetModel()
        self._rows.clear()
        self._by_key.clear()
        self.endResetModel()
        self.countChanged.emit()


# ── Meldungs-Banner ──────────────────────────────────────────────────────────
class MessageBanner(_StateBase):
    """Ein Meldungs-Feed oben: Meldungen laufen nacheinander durch.

    Portierung von _msgbanner.js. Die Zeiten sind dieselben: 2,8 s Standzeit,
    danach 0,45 s Pause, bevor die naechste kommt. Die Warteschlange ist auf 10
    Eintraege begrenzt, damit ein Meldungsstau nicht minutenlang nachlaeuft.

    ⚠ Im Gesamt-Overlay teilen sich Rennleitung UND Undercut-Alarm diesen einen
    Banner (in templates/index.html gibt es genau ein #race-msg). Die getrennten
    Seiten /part/racemsg und /part/undercut existieren nur, damit man sie in OBS
    getrennt platzieren kann. Hier ist es wieder EIN Banner - wie im Gesamt-Overlay.
    """

    SHOW_MS = 2800
    GAP_MS = 450
    MAX_QUEUE = 10

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    iconChanged, icon = _prop("icon", str, "")
    textChanged, text = _prop("text", str, "")
    accentChanged, accent = _prop("accent", str, "#00e676")

    def __init__(self, parent=None):
        super().__init__(parent)
        self._queue = []
        self._busy = False
        self._enabled = True
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._hide)
        self._gap = QTimer(self)
        self._gap.setSingleShot(True)
        self._gap.setInterval(self.GAP_MS)
        self._gap.timeout.connect(self._next)

    def set_enabled(self, on: bool) -> None:
        """CFG.msgs - Meldungen lassen sich in den Settings abschalten."""
        self._enabled = bool(on)
        if not on:
            self._queue.clear()

    def push(self, icon: str, text: str, accent: str) -> None:
        if not self._enabled:
            return
        self._queue.append((icon, text, accent))
        if len(self._queue) > self.MAX_QUEUE:
            self._queue.pop(0)
        self._next()

    def _next(self) -> None:
        if self._busy or not self._queue:
            return
        self._busy = True
        icon, text, accent = self._queue.pop(0)
        self._assign({"icon": icon, "text": text, "accent": accent, "isVisible": True})
        self._timer.start(self.SHOW_MS)

    def _hide(self) -> None:
        self.isVisible = False
        self._busy = False
        self._gap.start()


class RaceControl:
    """Rennleitungs-Meldungen aus dem race_control-Feed (racemsg.js)."""

    # Typ -> (Symbol, Farbe). "fl" fehlt bewusst: dafuer gibt es das grosse Banner.
    STYLES = {
        "penalty":    ("⚑", "#ff5a5a"),
        "tracklimit": ("⚠", "#ffcc00"),
        "mom":        ("⚡", "#2fd9ff"),
        "retire":     ("✖", "#b0b6c0"),
        "redflag":    ("⚑", "#ff2a1a"),
        "winner":     ("🏆", "#ffd700"),
        "flag":       ("🏁", "#ffffff"),
        "penserved":  ("✓", "#00e676"),
    }

    def __init__(self, banner: MessageBanner):
        self._banner = banner
        self._last_id = 0

    def update(self, data: dict) -> None:
        rc = data.get("race_control") or []
        if rc and rc[-1].get("id", 0) < self._last_id:
            self._last_id = 0            # neue Session -> die Ids fangen von vorn an
        for msg in rc:
            if msg.get("id", 0) <= self._last_id:
                continue
            self._last_id = msg["id"]
            kind = msg.get("type")
            if kind == "fl":
                continue                 # uebernimmt das Fastest-Lap-Banner
            icon, color = self.STYLES.get(kind, ("•", "#9aa0aa"))
            self._banner.push(icon, msg.get("text") or "", color)

    def on_safety_car(self, status: str, prev: str) -> None:
        if status in ("sc", "vsc"):
            self._banner.push("⚠", "SAFETY CAR" if status == "sc" else "VIRTUAL SAFETY CAR",
                              "#ffcc00")
        elif status == "none" and prev in ("sc", "vsc"):
            self._banner.push("✓", "RENNEN FREIGEGEBEN", "#00e676")


class Undercut:
    """Undercut-Erkennung (undercut.js) - meldet in denselben Banner.

    Fahrer B boxt weniger als 2,5 s hinter A -> Versuch. Sobald A auch geboxt hat
    und wieder draussen ist, entscheidet die Position, ob er aufging.
    """

    GAP_MAX = 2.5
    LAPS_MAX = 6        # boxt der Gegner nicht, verjaehrt der Versuch

    def __init__(self, banner: MessageBanner):
        self._banner = banner
        self._in_pit = {}
        self._attempts = []

    def update(self, drivers, session: dict, connected: bool, enabled: bool) -> None:
        if not enabled or session.get("is_quali") or not connected:
            self._attempts = []
            return
        by_idx = {d["index"]: d for d in drivers}
        for d in drivers:
            was = self._in_pit.get(d["index"], False)
            if d.get("in_pit") and not was:
                ahead = next((x for x in drivers
                              if x.get("position") == (d.get("position") or 0) - 1), None)
                gap = d.get("gap_to_ahead") or 0
                if ahead and not ahead.get("in_pit") and 0 < gap < self.GAP_MAX:
                    self._attempts.append({
                        "a": ahead["index"], "b": d["index"],
                        "an": ahead.get("name") or "", "bn": d.get("name") or "",
                        "phase": 1, "lap0": session.get("current_lap") or 0,
                    })
                    self._banner.push(
                        "⛏", f"UNDERCUT-VERSUCH: {d.get('name') or ''} auf "
                             f"{ahead.get('name') or ''}", "#ffcc00")
            self._in_pit[d["index"]] = bool(d.get("in_pit"))

        keep = []
        for u in self._attempts:
            a, b = by_idx.get(u["a"]), by_idx.get(u["b"])
            if not a or not b or a.get("dnf") or b.get("dnf") \
                    or a.get("dsq") or b.get("dsq"):
                continue
            if (session.get("current_lap") or 0) - u["lap0"] > self.LAPS_MAX:
                continue
            if u["phase"] == 1 and a.get("in_pit"):
                u["phase"] = 2                       # der Gegner ist in der Box
            if u["phase"] == 2 and not a.get("in_pit"):
                if (b.get("position") or 99) < (a.get("position") or 99):
                    self._banner.push("✓", f"UNDERCUT FUNKTIONIERT: {u['bn']} vor {u['an']}",
                                      "#00e676")
                else:
                    self._banner.push("✗", f"UNDERCUT GESCHEITERT: {u['an']} bleibt vor "
                                           f"{u['bn']}", "#ff5a5a")
                continue
            keep.append(u)
        self._attempts = keep


# ── Battle-Boxen ─────────────────────────────────────────────────────────────
BATTLE_ROLES = ["slot", "driverIndex", "position", "name", "teamColor",
                "tyreIcon", "tyreStamp", "gap", "isLead", "barWidth", "barColor",
                "fresh"]


class BattleBox(_StateBase):
    """Eine der vier Battle-Boxen."""

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    headerChanged, header = _prop("header", str, "")
    subLapsChanged, subLaps = _prop("subLaps", int, 0)
    subVisibleChanged, subVisible = _prop("subVisible", bool, False)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows = SlotModel(BATTLE_ROLES, self)
        self._members = ""
        self._born = 0.0
        self._tyre_stamp = {}

    @Property(QObject, constant=True)
    def rows(self):
        return self._rows


class Battles(QObject):
    """Kampfgruppen erkennen und auf vier Boxen verteilen (battles.js).

    Die Ruhe-Regeln aus dem Original sind mitgenommen, sonst flackert das Ding:
      * Hysterese: ein Link entsteht unter `battlethresh`, loest sich erst 0,6 s daueber
      * Reifezeit: eine Gruppe erscheint erst, wenn sie 2 s Bestand hat
      * Sticky: eine Box behaelt "ihre" Gruppe, statt dass die Boxen durchtauschen
      * Mindeststandzeit: eine sichtbare Box bleibt mindestens 4 s
    """

    BOX_COUNT = 4
    MAX_ROWS = 6
    GROUP_READY_MS = 2000
    MIN_SHOW_MS = 4000
    EXIT_MARGIN = 0.6

    # Weichere Mischung = kleinerer Wert (battles.js CMP_SOFT)
    CMP_SOFT = {"S": 0, "M": 1, "H": 2, "I": 0, "W": 1}

    changed = Signal()

    def __init__(self, cfg, parent=None):
        super().__init__(parent)
        self._cfg = cfg
        self._boxes = [BattleBox(self) for _ in range(self.BOX_COUNT)]
        self._link = {}
        self._group_seen = {}
        self._gap_sample = {}
        self._pred = None
        self._stamp = 0

    @Property("QVariantList", constant=True)
    def boxes(self):
        return self._boxes

    # ── Ueberhol-Projektion ("Kampf in X Runden") ────────────────────────────
    def _update_prediction(self, drivers, active: bool) -> None:
        if not active:
            self._pred = None
            return
        now = time.monotonic()
        best = None
        for i, d in enumerate(drivers):
            if i == 0 or d.get("in_pit") or d.get("dnf") or d.get("dsq"):
                continue
            gap = d.get("gap_to_ahead") or 0
            prev = self._gap_sample.get(d["index"])
            if prev and now - prev[0] > 1.2:
                rate = (prev[1] - gap) / (now - prev[0])
                self._gap_sample[d["index"]] = (now, gap)
                if rate > 0.01 and 0.4 < gap < 6:
                    lap_ref = d["last_lap"] if (d.get("last_lap") or 0) > 30 else 90
                    laps = gap / (rate * lap_ref)
                    if 1 <= laps <= 9 and (best is None or laps < best["laps"]):
                        best = {"laps": round(laps), "a": drivers[i - 1]["index"],
                                "b": d["index"]}
            elif not prev:
                self._gap_sample[d["index"]] = (now, gap)
        self._pred = best

    # ── Gruppen finden ───────────────────────────────────────────────────────
    def _find_groups(self, drivers):
        groups, cur = [], []
        enter = self._cfg["battlethresh"] or 1.5
        exit_g = enter + self.EXIT_MARGIN

        def flush():
            if len(cur) >= 2:
                groups.append(list(cur))

        for d in drivers:
            if d.get("dnf") or d.get("dsq") or d.get("in_pit"):
                self._link[d["index"]] = False
                flush()
                cur = []
                continue
            if not cur:
                cur = [d]
                continue
            gap = d.get("gap_to_ahead") or 0
            linked = gap > 0 and (gap < enter or (self._link.get(d["index"]) and gap < exit_g))
            self._link[d["index"]] = linked
            if linked:
                cur.append(d)
            else:
                flush()
                cur = [d]
        flush()
        return groups

    def update(self, drivers, data: dict, session: dict, active: bool) -> None:
        self._stamp += 1
        self._update_prediction(
            drivers, self._cfg["pred"] and data.get("connected")
            and not session.get("is_quali"))

        now = _now_ms()
        groups = self._find_groups(drivers) if active else []
        groups.sort(key=lambda g: g[0].get("position") or 999)
        groups = [g[:self.MAX_ROWS] for g in groups[:self.BOX_COUNT + 2]]

        # Reifezeit: die Gruppe muss 2 s bestehen, bevor sie erscheint.
        seen, ready = set(), []
        for g in groups:
            sig = f"{g[0]['index']}-{g[1]['index']}"
            seen.add(sig)
            self._group_seen.setdefault(sig, now)
            if now - self._group_seen[sig] >= self.GROUP_READY_MS:
                ready.append(g)
        for sig in [s for s in self._group_seen if s not in seen]:
            del self._group_seen[sig]

        # Sticky: zuerst die Boxen bedienen, die diese Gruppe schon zeigen.
        assigned = [None] * self.BOX_COUNT
        used = set()
        members = [set(int(x) for x in (b._members.split(",") if b._members else []))
                   for b in self._boxes]
        for gi, g in enumerate(ready):
            best, best_overlap = -1, 1     # mindestens 2 gemeinsame Fahrer
            for k, box in enumerate(self._boxes):
                if assigned[k] is not None or not box.isVisible:
                    continue
                overlap = sum(1 for d in g if d["index"] in members[k])
                if overlap > best_overlap:
                    best, best_overlap = k, overlap
            if best >= 0:
                assigned[best] = g
                used.add(gi)
        for gi, g in enumerate(ready):
            if gi in used:
                continue
            free = next((k for k, box in enumerate(self._boxes)
                         if assigned[k] is None and not box.isVisible), -1)
            if free < 0:
                free = next((k for k in range(self.BOX_COUNT) if assigned[k] is None), -1)
            if free >= 0:
                assigned[free] = g
                used.add(gi)

        for k, box in enumerate(self._boxes):
            g = assigned[k]
            if g:
                if not box.isVisible:
                    box._born = now
                    box.isVisible = True
                self._render_box(box, g)
            else:
                # Mindestanzeige: eine sichtbare Box bleibt 4 s stehen.
                if box.isVisible and data.get("connected") \
                        and now - box._born < self.MIN_SHOW_MS:
                    continue
                if box.isVisible:
                    box.isVisible = False
                    box._members = ""
                    box.rows.clear()

    def _render_box(self, box: BattleBox, group) -> None:
        front, back = group[0], group[-1]
        box.header = (f"BATTLE FOR P{front.get('position')}" if len(group) == 2
                      else f"BATTLE · P{front.get('position')}–P{back.get('position')}")

        in_group = (self._pred is not None
                    and any(d["index"] == self._pred["a"] for d in group)
                    and any(d["index"] == self._pred["b"] for d in group))
        box.subVisible = in_group
        if in_group:
            box.subLaps = self._pred["laps"]

        entries = []
        member_sig = ",".join(str(x) for x in sorted(d["index"] for d in group))
        rebuilt = member_sig != box._members
        box._members = member_sig
        for i, d in enumerate(group):
            gap = None if i == 0 else (d.get("gap_to_ahead") or 0)
            compound = d.get("compound") or "?"
            prev = box._tyre_stamp.get(d["index"])
            if prev and prev[0] != compound:
                box._tyre_stamp[d["index"]] = (compound, self._stamp)
            elif not prev:
                box._tyre_stamp[d["index"]] = (compound, 0)
            entries.append((d["index"], {
                "driverIndex": d["index"],
                "position": d.get("position") or 0,
                "name": d.get("name") or "",
                "teamColor": _team_color(d.get("team") or ""),
                "tyreIcon": compound if compound in TYRE_ICONS else "",
                "tyreStamp": box._tyre_stamp[d["index"]][1],
                "gap": -1.0 if gap is None else float(gap),
                "isLead": gap is None,
                "barWidth": 0.0 if gap is None else self._bar_width(gap),
                "barColor": "#ffcc00" if gap is None else self._bar_color(gap),
                "fresh": self._fresh_badge(d, None if i == 0 else group[i - 1]),
            }))
        if rebuilt:
            box.rows.clear()
        box.rows.sync(entries)

    @staticmethod
    def _bar_width(gap: float) -> float:
        """0..1. Erst unter 1,0 s ueberhaupt sichtbar, voll bei 0 (battles.js barStyle)."""
        return max(0.0, min(1.0, (1.0 - gap) / 1.0))

    @classmethod
    def _bar_color(cls, gap: float) -> str:
        close = cls._bar_width(gap)
        return f"#ff{round(204 - 140 * close):02x}{round(64 * close):02x}"

    def _fresh_badge(self, d: dict, ahead) -> str:
        """"−N Rnd" am Jaeger, wenn er deutlich frischere Reifen hat (R3.3)."""
        if not self._cfg["fresh"] or not ahead or d.get("in_pit") or ahead.get("in_pit"):
            return ""
        diff = (ahead.get("tyre_age") or 0) - (d.get("tyre_age") or 0)
        softer = (self.CMP_SOFT.get(d.get("compound"), 9)
                  < self.CMP_SOFT.get(ahead.get("compound"), 9))
        if diff >= 5 or (softer and diff >= 3):
            return f"−{diff} Rnd"
        return ""


# ── Hotlap-Boxen ─────────────────────────────────────────────────────────────
HOTLAP_ROLES = ["slot", "driverIndex", "position", "name", "teamColor", "teamLogo",
                "tyreIcon", "tyreStamp", "invalid", "timeBase", "timeAt", "ticking",
                "staticTime", "delta", "deltaUp", "hasDelta",
                "sectors", "sectorClasses"]


class Hotlaps(QObject):
    """Wer faehrt gerade eine fliegende Runde (hotlap.js).

    Sortiert nach Streckenfortschritt: wer als Naechstes ueber die Linie kommt,
    steht links. Hoechstens vier Boxen.
    """

    MAX_BOXES = 4

    def __init__(self, shared, parent=None):
        super().__init__(parent)
        self._shared = shared
        self._rows = SlotModel(HOTLAP_ROLES, self)
        self._tyre = {}
        self._stamp = 0

    @Property(QObject, constant=True)
    def boxes(self):
        return self._rows

    def update(self, drivers, active: bool) -> None:
        self._stamp += 1
        if not active:
            self._rows.clear()
            return
        hot = [d for d in drivers
               if self._shared.quali_status(d) == "track"
               and not d.get("dnf") and not d.get("dsq")]
        hot.sort(key=lambda d: -(d.get("lap_distance") or 0))
        hot = hot[:self.MAX_BOXES]

        entries = []
        for d in hot:
            idx = d["index"]
            secs = [d.get("sector1") or 0, d.get("sector2") or 0, _s3_of(d)]
            done = sum(1 for v in secs if v > 0)

            # Live-Delta gegen die Session-Bestsektoren bis zum fertigen Sektor.
            delta, have = 0.0, False
            for i in range(done):
                if self._shared.sec_best[i] < INF:
                    delta += secs[i] - self._shared.sec_best[i]
                    have = True

            compound = d.get("compound") or "?"
            prev = self._tyre.get(idx)
            if prev and prev[0] != compound:
                self._tyre[idx] = (compound, self._stamp)
            elif not prev:
                self._tyre[idx] = (compound, 0)

            clt = d.get("current_lap_time") or 0
            classes = [self._sec_class(secs[i], idx, i) for i in range(3)]
            entries.append((idx, {
                "driverIndex": idx,
                "position": d.get("position") or 0,
                "name": d.get("name") or "",
                "teamColor": _team_color(d.get("team") or ""),
                "teamLogo": TEAM_LOGOS.get(d.get("team") or "", ""),
                "tyreIcon": compound if compound in TYRE_ICONS else "",
                "tyreStamp": self._tyre[idx][1],
                "invalid": bool(d.get("lap_invalid")),
                # Die grosse Zeit zaehlt in QML weiter: Basiswert + Zeitstempel, der
                # Rest ist Uhr. Genau wie tickHotlapTimes() im Original.
                "timeBase": float(clt),
                "timeAt": _now_ms() if clt > 0 else 0.0,
                "ticking": clt > 0,
                "staticTime": float(d.get("last_lap") or 0),
                "delta": float(delta),
                "deltaUp": delta <= 0.0005,
                "hasDelta": have,
                "sectors": list(secs),
                "sectorClasses": classes,
            }))
        self._rows.sync(entries)

    def _sec_class(self, v: float, idx: int, i: int) -> str:
        """Nur LESEND - die Bestzeiten pflegt derive.py."""
        if not v or v <= 0:
            return ""
        if v <= self._shared.sec_best[i] + 0.005:
            return "sp"
        pb = self._shared.drv_sec_best.get(idx)
        if pb and v <= pb[i] + 0.005:
            return "sg"
        return "sy"


# ── Onboard ──────────────────────────────────────────────────────────────────
class Onboard(_StateBase):
    """Telemetrie des Kamera-Fahrers (onboard.js).

    Quali: der Fahrer, auf den die Kamera schaut. Rennen: nur im Safety-Car-Fenster,
    und dann IMMER P1 - unabhaengig davon, wen die Kamera zeigt.
    """

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    nameChanged, name = _prop("name", str, "")
    positionChanged, position = _prop("position", int, 0)
    teamColorChanged, teamColor = _prop("teamColor", str, "#ffffff")
    teamLogoChanged, teamLogo = _prop("teamLogo", str, "")
    speedChanged, speed = _prop("speed", int, 0)
    gearChanged, gear = _prop("gear", str, "N")
    throttleChanged, throttle = _prop("throttle", float, 0.0)
    brakeChanged, brake = _prop("brake", float, 0.0)
    # Delta-Balken
    deltaVisibleChanged, deltaVisible = _prop("deltaVisible", bool, False)
    deltaChanged, delta = _prop("delta", float, 0.0)
    hasDeltaChanged, hasDelta = _prop("hasDelta", bool, False)
    # Sektor-Ampel
    ampVisibleChanged, ampVisible = _prop("ampVisible", bool, False)
    ampChanged = Signal()

    def __init__(self, shared, cfg, parent=None):
        super().__init__(parent)
        self._shared = shared
        self._cfg = cfg
        self._amp = ["", "", ""]

    @Property("QVariantList", notify=ampChanged)
    def amp(self):
        """Drei Segmente: "sp"/"sg"/"sy" gefaerbt, "run" = laufender Sektor, "" = leer."""
        return self._amp

    def update(self, drivers, focus_index, connected: bool, session: dict,
               enabled: bool) -> None:
        sh = self._shared
        race_window = sh.sc_onboard_active or sh.sc_restart_until_lap > 0
        is_quali = bool(session.get("is_quali"))
        visible = enabled and connected and (
            (focus_index is not None and focus_index >= 0) if is_quali else race_window)
        if not visible:
            self.isVisible = False
            return

        if is_quali:
            d = next((x for x in drivers if x["index"] == focus_index), None)
        else:
            d = next((x for x in drivers if x.get("position") == 1), None)
            # Fenster schliesst, wenn P1 nach dem Restart ueber Start/Ziel ist.
            if sh.sc_restart_until_lap > 0 and d \
                    and (d.get("lap_num") or 0) >= sh.sc_restart_until_lap:
                sh.sc_restart_until_lap = 0
                sh.sc_onboard_active = False
                self.isVisible = False
                return
        if not d:
            self.isVisible = False
            return

        gear = d.get("gear", 0)
        on_hotlap = is_quali and sh.quali_status(d) == "track"

        # Delta zur persoenlichen Bestrunde - nur in der Quali auf einem Hotlap.
        delta, have = 0.0, False
        if on_hotlap:
            pb = sh.drv_sec_best.get(d["index"])
            done = min(d.get("sector") or 0, 2)
            if pb:
                secs = [d.get("sector1") or 0, d.get("sector2") or 0]
                for i in range(done):
                    if secs[i] > 0 and pb[i] < INF:
                        delta += secs[i] - pb[i]
                        have = True

        self._assign({
            "isVisible": True,
            "name": d.get("name") or "",
            "position": d.get("position") or 0,
            "teamColor": _team_color(d.get("team") or ""),
            "teamLogo": TEAM_LOGOS.get(d.get("team") or "", ""),
            "speed": int(d.get("speed") or 0),
            "gear": "R" if gear == -1 else "N" if gear == 0 else str(gear),
            "throttle": float(d.get("throttle") or 0),
            "brake": float(d.get("brake") or 0),
            "deltaVisible": bool(self._cfg["deltabar"]) and on_hotlap,
            "delta": float(delta),
            "hasDelta": have,
            "ampVisible": on_hotlap and self._cfg["ampel"] is not False,
        })

        # Sektor-Ampel: S1/S2 live, S3 aus der zuletzt beendeten Runde.
        if self.ampVisible:
            pb = sh.drv_sec_best.get(d["index"])
            done = min(d.get("sector") or 0, 2)
            s3 = 0 if (d.get("sector") or 0) >= 2 else sh.cur_s3.get(d["index"], 0)
            secs = [d.get("sector1") or 0, d.get("sector2") or 0, s3]
            amp = []
            for i, v in enumerate(secs):
                if v > 0:
                    pbv = pb[i] if pb else INF
                    amp.append("sp" if v <= sh.sec_best[i] + 0.005
                               else "sg" if v <= pbv + 0.005 else "sy")
                elif i == done:
                    amp.append("run")
                else:
                    amp.append("")
            if amp != self._amp:
                self._amp = amp
                self.ampChanged.emit()


# ── Lower-Third ──────────────────────────────────────────────────────────────
class LowerThird(_StateBase):
    """Namens-Tag beim Kamerawechsel (lowerthird.js). Nur im Rennen ab Runde 3."""

    # Reifenfarben fuer Mischungen ohne eigenes Bild
    TYRE_HEX = {"S": "#E8002D", "M": "#FFC906", "H": "#EBEBEB",
                "I": "#39B54A", "W": "#0067FF"}

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    nameChanged, name = _prop("name", str, "")
    positionChanged, position = _prop("position", int, 0)
    teamChanged, team = _prop("team", str, "")
    teamColorChanged, teamColor = _prop("teamColor", str, "#ffffff")
    tyreIconChanged, tyreIcon = _prop("tyreIcon", str, "")
    ageTextChanged, ageText = _prop("ageText", str, "")
    gapTextChanged, gapText = _prop("gapText", str, "")
    stintsChanged = Signal()

    def __init__(self, shared, cfg, parent=None):
        super().__init__(parent)
        self._shared = shared
        self._cfg = cfg
        self._stints = []
        self._focus = None
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(lambda: setattr(self, "isVisible", False))

    @Property("QVariantList", notify=stintsChanged)
    def stints(self):
        """[{icon, color, letter, laps}] - Reifenbild wenn vorhanden, sonst Buchstabe."""
        return self._stints

    def update(self, drivers, focus_index, session: dict, connected: bool,
               enabled: bool) -> None:
        if not enabled or not connected or session.get("is_quali") \
                or (session.get("current_lap") or 0) < 3:
            self.isVisible = False
            self._focus = focus_index
            return
        if focus_index == self._focus:
            return                      # nur beim WECHSEL einblenden
        self._focus = focus_index
        d = next((x for x in drivers if x["index"] == focus_index), None)
        if not d:
            return

        compound = d.get("compound") or ""
        age = d.get("tyre_age") or 0
        laps_down = d.get("laps_down") or 0
        gap = (
            "LEADER" if d.get("position") == 1
            else f"+{laps_down} LAP" + ("S" if laps_down > 1 else "") if laps_down >= 1
            else ("+%.3f" % (d.get("gap_to_ahead") or 0)) if (d.get("gap_to_ahead") or 0) > 0
            else "0.000")

        stints = []
        if self._cfg["strat"] and compound:
            raw = list(self._shared.stint_len.get(d["index"], []))
            raw.append({"c": compound, "laps": age})
            for s in raw:
                if not s.get("c"):
                    continue
                stints.append({
                    "icon": s["c"] if s["c"] in TYRE_ICONS else "",
                    "letter": s["c"],
                    "color": self.TYRE_HEX.get(s["c"], "#888888"),
                    "laps": s.get("laps") or 0,
                })
        if stints != self._stints:
            self._stints = stints
            self.stintsChanged.emit()

        self._assign({
            "name": d.get("name") or "",
            "position": d.get("position") or 0,
            "team": d.get("team") or "",
            "teamColor": _team_color(d.get("team") or ""),
            "tyreIcon": compound if compound in TYRE_ICONS else "",
            "ageText": f"· Rnd {age}" if compound and age >= 0 else "",
            "gapText": gap,
            "isVisible": True,
        })
        self._timer.start(int((self._cfg["ltdur"] or 4) * 1000))


# ── Boxenstopp-Karten ────────────────────────────────────────────────────────
PIT_ROLES = ["slot", "cardId", "name", "teamColor", "live", "t0", "finalTime",
             "oldTyre", "newTyre", "showArrow", "newWing", "exitPos"]


class PitCards(QObject):
    """Live-Boxenstopp-Timer (pit.js). Hoechstens drei Karten gleichzeitig.

    ⚠ Im Gesamt-Overlay steckt die Boxen-Erkennung mitten in der Tower-Schleife
    (test.html Z. 2148-2168). Hier laeuft sie wie in pit.js als eigene Schleife.
    """

    MAX_CARDS = 3
    PIT_LOSS = 22            # grobe Schaetzung des Zeitverlusts eines Stopps
    FW_CHANGE_MIN = 10       # ab diesem Schaden gilt ein Fluegel als gewechselt
    HOLD_MS = 5000           # so lange bleibt die fertige Karte stehen

    def __init__(self, parent=None):
        super().__init__(parent)
        self._cards = SlotModel(PIT_ROLES, self)
        self._track = {}
        self._next_id = 1
        self._order = []      # Karten-Ids in Anzeigereihenfolge

    @Property(QObject, constant=True)
    def cards(self):
        return self._cards

    def update(self, drivers, session: dict, connected: bool) -> None:
        if not connected:
            return
        for d in drivers:
            idx = d["index"]
            st = self._track.setdefault(idx, {"in": False, "comp": d.get("compound"),
                                              "last_comp": d.get("compound"),
                                              "card": None, "fw": 0})
            # Den Compound AUSSERHALB der Box mitschreiben - das ist der wahre
            # "alte" Reifen. Manche Quellen melden den neuen schon bei der Einfahrt.
            if not d.get("in_pit"):
                st["last_comp"] = d.get("compound")

            if d.get("in_pit") and not st["in"]:
                st["in"] = True
                st["comp"] = st["last_comp"]
                st["fw"] = max(d.get("dmg_fl") or 0, d.get("dmg_fr") or 0)
                # Nur im Rennen ab Runde 2 - der Startaufstellungs-Reifen zaehlt nicht.
                if not session.get("is_quali") and (session.get("current_lap") or 0) >= 2 \
                        and not d.get("dnf") and not d.get("dsq"):
                    st["card"] = self._start(d, drivers)
                else:
                    st["card"] = None
            elif not d.get("in_pit") and st["in"]:
                st["in"] = False
                if st["card"]:
                    self._finish(st["card"], d, st["comp"], d.get("compound"), st["fw"])
                    st["card"] = None

            if (d.get("dnf") or d.get("dsq")) and st["card"]:
                self._drop(st["card"])
                st["card"] = None
        self._flush()

    def _start(self, d: dict, drivers) -> int:
        card_id = self._next_id
        self._next_id += 1
        # Ungefaehre Auskommen-Position: Gap plus Pit-Verlust ins Feld einsortieren.
        proj_gap = (d.get("gap_to_leader") or 0) + self.PIT_LOSS
        proj_pos = 1
        for o in drivers:
            if o["index"] != d["index"] and not o.get("dnf") and not o.get("dsq") \
                    and (o.get("gap_to_leader") or 0) < proj_gap:
                proj_pos += 1
        self._order.append({
            "cardId": card_id, "name": d.get("name") or "",
            "teamColor": _team_color(d.get("team") or ""),
            "live": True, "t0": _now_ms(), "finalTime": 0.0,
            "oldTyre": "", "newTyre": (d.get("compound") or "")
                                      if (d.get("compound") in TYRE_ICONS) else "",
            "showArrow": False, "newWing": False, "exitPos": proj_pos,
            "_expires": 0.0,
        })
        while len(self._order) > self.MAX_CARDS:
            self._order.pop(0)
        return card_id

    def _card(self, card_id):
        return next((c for c in self._order if c["cardId"] == card_id), None)

    def _finish(self, card_id: int, d: dict, old_c, new_c, entry_fw: int) -> None:
        card = self._card(card_id)
        if card is None:
            return
        swapped = (old_c in TYRE_ICONS and new_c in TYRE_ICONS and old_c != new_c)
        cur_fw = max(d.get("dmg_fl") or 0, d.get("dmg_fr") or 0)
        card.update({
            "live": False,
            "finalTime": float(d.get("pit_time") or 0),
            "oldTyre": old_c if swapped else "",
            "newTyre": (new_c or old_c) if (new_c or old_c) in TYRE_ICONS else "",
            "showArrow": swapped,
            # Neuer Fluegel: war bei der Einfahrt beschaedigt, ist bei der Ausfahrt
            # (fast) heil - das Spiel setzt den Schaden dann auf ~0.
            "newWing": (entry_fw or 0) >= self.FW_CHANGE_MIN and cur_fw <= entry_fw * 0.5,
            "_expires": _now_ms() + self.HOLD_MS,
        })

    def _drop(self, card_id: int) -> None:
        self._order = [c for c in self._order if c["cardId"] != card_id]

    def _flush(self) -> None:
        now = _now_ms()
        self._order = [c for c in self._order
                       if c["_expires"] == 0.0 or now < c["_expires"]]
        self._cards.sync([(c["cardId"], {k: v for k, v in c.items()
                                         if not k.startswith("_")})
                          for c in self._order])


# ── Kleine Anzeigen ──────────────────────────────────────────────────────────
class StartLights(_StateBase):
    """Die fuenf Startlichter (lights.js).

    STLG meldet die Zahl leuchtender Lichter (1..5), LGOT = alle aus. `age` ist die
    Zeit seit dem letzten Ereignis - danach blendet die Anzeige von selbst aus.
    """

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    litChanged, lit = _prop("lit", int, 0)
    lightsOutChanged, lightsOut = _prop("lightsOut", bool, False)

    def update(self, sl, connected: bool, enabled: bool) -> None:
        if not enabled or not sl or not connected:
            self._assign({"isVisible": False, "lightsOut": False})
            return
        num, out, age = sl.get("num", 0), sl.get("out"), sl.get("age", 9999)
        if out:
            show = age < 3.0
            self._assign({"isVisible": show, "lightsOut": show, "lit": 0})
        elif num > 0 and age < 8.0:
            self._assign({"isVisible": True, "lightsOut": False, "lit": int(num)})
        else:
            self._assign({"isVisible": False, "lightsOut": False})


class FastestLap(_StateBase):
    """Das lila Bestrunden-Banner (flbanner.js)."""

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    nameChanged, name = _prop("name", str, "")
    lapTimeChanged, lapTime = _prop("lapTime", float, 0.0)

    def __init__(self, cfg, parent=None):
        super().__init__(parent)
        self._cfg = cfg
        self._sig = None
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(lambda: setattr(self, "isVisible", False))

    def update(self, session: dict, connected: bool, enabled: bool) -> None:
        if not enabled or not connected or session.get("is_quali"):
            self.isVisible = False
            return
        if not (session.get("fastest_lap_time") or 0) > 0:
            return
        sig = f"{session.get('fastest_lap_driver') or ''}|{session['fastest_lap_time']}"
        if sig == self._sig:
            return
        first = self._sig is None    # mitten in der Session gestartet -> nicht feuern
        self._sig = sig
        if first:
            return
        self._assign({"name": session.get("fastest_lap_driver") or "",
                      "lapTime": float(session["fastest_lap_time"]),
                      "isVisible": True})
        self._timer.start(int((self._cfg["flbdur"] or 4.5) * 1000))


class DangerZone(_StateBase):
    """Countdown auf dem letzten sicheren Platz (danger.js).

    Nur in den letzten drei Minuten von Q1 und Q2.
    """

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    cutChanged, cut = _prop("cut", int, 0)
    nameChanged, name = _prop("name", str, "")
    timeLeftChanged, timeLeft = _prop("timeLeft", int, 0)

    def update(self, drivers, session: dict, connected: bool, enabled: bool) -> None:
        cut = 16 if session.get("type") == 5 else 10 if session.get("type") == 6 else 0
        left = session.get("time_left") or 0
        if not enabled or not connected or not session.get("is_quali") or not cut \
                or left <= 0 or left > 180:
            self.isVisible = False
            return
        bubble = next((d for d in drivers if d.get("position") == cut), None)
        if not bubble:
            self.isVisible = False
            return
        self._assign({"cut": cut, "name": bubble.get("name") or "",
                      "timeLeft": int(left), "isVisible": True})


class PitProjection(_StateBase):
    """"Wo kaeme er nach einem Stopp raus" (pitproj.js).

    Erscheint nur, wenn der Kamera-Fahrer Schaden ab 30 % hat - er muss also
    ohnehin zum Fluegelwechsel.
    """

    PIT_LOSS = 22
    DMG_MIN = 30

    isVisibleChanged, isVisible = _prop("isVisible", bool, False)
    nameChanged, name = _prop("name", str, "")
    currentChanged, current = _prop("current", int, 0)
    projectedChanged, projected = _prop("projected", int, 0)

    def update(self, drivers, focus_index, session: dict, connected: bool,
               enabled: bool) -> None:
        if not enabled or not connected or session.get("is_quali"):
            self.isVisible = False
            return
        d = next((x for x in drivers if x["index"] == focus_index), None)
        dmg = max(d.get("dmg_fl") or 0, d.get("dmg_fr") or 0,
                  d.get("dmg_rw") or 0) if d else 0
        if not d or d.get("dnf") or d.get("dsq") or d.get("in_pit") or dmg < self.DMG_MIN:
            self.isVisible = False
            return
        proj_gap = (d.get("gap_to_leader") or 0) + self.PIT_LOSS
        proj_pos = 1
        for o in drivers:
            if o["index"] != d["index"] and not o.get("dnf") and not o.get("dsq") \
                    and (o.get("gap_to_leader") or 0) < proj_gap:
                proj_pos += 1
        self._assign({"name": d.get("name") or "", "current": d.get("position") or 0,
                      "projected": proj_pos, "isVisible": True})
