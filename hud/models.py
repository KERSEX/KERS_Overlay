"""
Die Qt-Modelle, an denen das QML haengt.

Grundgedanke: QML soll NUR darstellen. Alles, was gerechnet, verglichen oder
gemerkt werden muss, passiert vorher - in derive.py und hier. Ein QML-Delegate,
der pro Bild dreimal durch die Fahrerliste laeuft, ist bei 22 Zeilen und 30 Hz
genau die Sorte Arbeit, die man nicht in der Szene haben will.

Enthalten:
    DriverModel     die Fahrerzeilen. Zeilen bleiben an ihrem Platz, die SICHTBARE
                    Reihenfolge steht in der Rolle `slot` - siehe Kommentar dort.
    SessionState    Kopfzeile: Titel, Runde, Flaggen, Wetter, Safety Car
    SettingsState   die Settings vom Server als Properties (Branding, Deckkraft, ...)
    RegieState      die manuellen Einblendungen aus /regie
"""

from PySide6.QtCore import (QAbstractListModel, QByteArray, QModelIndex, QObject,
                            Property, Qt, Signal, Slot)

# Muss zu ROW_HEIGHT in static/parts/tower.js passen.
ROW_HEIGHT = 62

# Wie viele Payloads ein Fahrer fehlen darf, bevor seine Zeile verschwindet.
# 1:1 aus tower.js: ein einzelner unvollstaendiger Payload soll keine Zeile
# wegreissen (das waren die Geisterzeilen ueber P19/P20).
MISS_TOLERANCE = 3

# Teamfarben - identisch zu :root in static/css/core.css.
TEAM_COLORS = {
    "Red Bull": "#3671C6", "McLaren": "#FF8000", "Ferrari": "#E80020",
    "Mercedes": "#27F4D2", "Aston Martin": "#229971", "Haas": "#B6BABD",
    "RB": "#6692FF", "Alpine": "#0093CC", "Williams": "#64C4FF",
    "Sauber": "#52E252", "Audi": "#C8102E", "Cadillac": "#C9A227",
}

# Dateinamen in static/teams/ - identisch zu TEAM_LOGOS in static/js/core.js.
TEAM_LOGOS = {
    "Red Bull": "Red_Bull_Half", "Ferrari": "Ferrari", "McLaren": "McLaren",
    "Mercedes": "Mercedes", "Aston Martin": "Aston_Martin", "Haas": "Haas",
    "RB": "Racing_Bull", "Alpine": "Alpine", "Williams": "Williams",
    "Sauber": "Audi", "Audi": "Audi", "Cadillac": "Cadillac",
}

TYRE_ICONS = {"S", "M", "H", "I", "W"}


class DriverModel(QAbstractListModel):
    """Die Fahrerzeilen des Towers.

    ⚠ WARUM DIE ZEILEN NICHT UMSORTIERT WERDEN
    Naheliegend waere, das Modell nach Position zu sortieren und Qt die Zeilen
    verschieben zu lassen. Dann baut QML die Delegates aber um, und genau die
    Bewegung, die man sehen WILL (P5 gleitet an P4 vorbei), findet nicht statt -
    stattdessen springt der Inhalt zwischen zwei stehenden Zeilen um.

    Deshalb dasselbe Verfahren wie im Web-Overlay: jede Zeile gehoert dauerhaft zu
    EINEM Fahrer (Schluessel ist sein UDP-Index), und die sichtbare Position steht
    in der Rolle `slot`. Das QML setzt `y: slot * ROW_HEIGHT` und laesst eine
    Behavior-Animation den Rest machen. Zeilen kommen nur beim Ein- und Aussteigen
    dazu oder weg.
    """

    ROLES = [
        # Identitaet und Platz
        "driverIndex", "slot", "position", "name", "team", "teamColor", "teamLogo",
        # Zustand fuer die Zeilenoptik
        "even", "isFocused", "isFinished", "isFastestLap", "elimZone", "lapInvalid",
        # Positionswechsel: Richtung + Zeitstempel (der Stempel loest die Animation aus)
        "changeDir", "changeStamp",
        # Bestrunde-Flash: "g" (persoenlich) / "p" (Session) + Zeitstempel
        "flashKind", "flashStamp",
        # Zeitenspalten (roh - formatiert wird im Delegate)
        "gapToLeader", "gapToAhead", "bestLap", "lastLap", "lapsDown",
        "dnf", "dsq", "inPit",
        # Reifen
        "compound", "tyreAge", "tyreIcon", "tyreStamp",
        # Sektoren: Zeiten + Farbklassen ("sp" lila / "sg" gruen / "sy" gelb / "")
        "sectors", "sectorClasses",
        # Rechte Spalte
        "qualiStatus", "drs", "overtakeActive", "overtakeAvailable",
        # Anbauten
        "penalties", "penDt", "cornerWarnings", "comeback",
        "dmgFl", "dmgFr", "dmgRw",
    ]

    countChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._roles = {Qt.ItemDataRole.UserRole + i: QByteArray(name.encode())
                       for i, name in enumerate(self.ROLES)}
        self._role_of = {name: Qt.ItemDataRole.UserRole + i
                         for i, name in enumerate(self.ROLES)}
        self._rows = []            # Liste von dicts, Reihenfolge = Anlegereihenfolge
        self._by_index = {}        # UDP-Fahrerindex -> Zeilen-dict
        self._miss = {}            # UDP-Fahrerindex -> Payloads ohne diesen Fahrer
        self._stamp = 0            # zaehlt hoch, dient als Ausloeser fuer Animationen

    # ── Qt-Modellschnittstelle ───────────────────────────────────────────────
    def roleNames(self):
        return self._roles

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._rows)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self._rows):
            return None
        name = self._roles.get(role)
        if name is None:
            return None
        return self._rows[index.row()].get(bytes(name).decode())

    @Property(int, notify=countChanged)
    def count(self):
        return len(self._rows)

    @Slot(int, result="QVariant")
    def get(self, row):
        """Eine ganze Zeile als Objekt - praktisch fuer Fremdzugriffe aus QML."""
        if 0 <= row < len(self._rows):
            return dict(self._rows[row])
        return {}

    # ── Aktualisieren ────────────────────────────────────────────────────────
    def update(self, drivers, shared, session, focus_index, cfg):
        """Einen Payload einarbeiten.

        `drivers` ist die bereits sortierte Liste aus SharedState.derive(); der
        Listenplatz IST die sichtbare Zeile (`slot`).
        """
        self._stamp += 1
        is_quali = bool(session.get("is_quali"))
        fastest = session.get("fastest_lap_driver") or ""

        # Quali: Elimination-Schnitt je Segment (tower.js)
        elim_cut = 0
        if is_quali:
            elim_cut = 16 if session.get("type") == 5 else 10 if session.get("type") == 6 else 0

        seen = set()
        for slot, d in enumerate(drivers):
            idx = d["index"]
            seen.add(idx)
            self._miss.pop(idx, None)
            row = self._by_index.get(idx)
            is_new = row is None
            if is_new:
                row = {"driverIndex": idx, "slot": slot, "changeDir": "none",
                       "changeStamp": 0, "flashKind": "", "flashStamp": 0,
                       "tyreStamp": 0, "_prevSlot": slot, "_prevBestLap": 0.0,
                       "_prevCompound": "", "_lastMove": 0, "_pos": len(self._rows),
                       "_new": True}
            # Erst befuellen, dann einfuegen: beginInsertRows laesst QML den Delegate
            # sofort bauen, und der soll die richtigen Werte sehen und nicht eine
            # leere Zeile, die einen Sekundenbruchteil spaeter nachgereicht wird.
            changed = self._fill_row(row, d, slot, shared, session, focus_index, cfg,
                                     is_quali, fastest, elim_cut)
            if is_new:
                self.beginInsertRows(QModelIndex(), len(self._rows), len(self._rows))
                self._rows.append(row)
                self._by_index[idx] = row
                self.endInsertRows()
                self.countChanged.emit()
            elif changed:
                # Nur die Rollen melden, die sich wirklich geaendert haben. Meldet man
                # pauschal alle, rechnet QML pro Payload saemtliche Bindings aller
                # Zeilen neu - bei 22 Zeilen und 30 Hz ist das genau die Arbeit, die
                # dem Spiel daneben fehlt.
                pos = self.index(row["_pos"], 0)
                self.dataChanged.emit(pos, pos, [self._role_of[r] for r in changed])

        # Fahrer, die im Payload fehlen: erst ein paar Umlaeufe tolerieren, dann raus.
        for idx in [i for i in self._by_index if i not in seen]:
            self._miss[idx] = self._miss.get(idx, 0) + 1
            if self._miss[idx] >= MISS_TOLERANCE:
                self._remove(idx)

    def _remove(self, idx) -> None:
        row = self._by_index.pop(idx, None)
        self._miss.pop(idx, None)
        if row is None:
            return
        # ⚠ NICHT self._rows.index(row): dicts vergleicht Python inhaltlich, zwei
        # zufaellig gleiche Zeilen wuerden die falsche treffen. Deshalb der
        # mitgefuehrte Platz.
        pos = row["_pos"]
        self.beginRemoveRows(QModelIndex(), pos, pos)
        self._rows.pop(pos)
        self.endRemoveRows()
        for later in self._rows[pos:]:
            later["_pos"] -= 1
        self.countChanged.emit()

    def _fill_row(self, row, d, slot, shared, session, focus_index, cfg,
                  is_quali, fastest, elim_cut) -> list:
        """Zeile aktualisieren. Liefert die Namen der tatsaechlich geaenderten Rollen."""
        stamp = self._stamp
        is_new = row.pop("_new", False)

        new = {}

        # Positionswechsel -> Pfeil + Aufblitzen der Nummer.
        # Wie in tower.js: der Pfeil bleibt 5 s stehen, danach wieder neutral.
        prev_slot = row["_prevSlot"]
        if not is_new and prev_slot != slot:
            new["changeDir"] = "up" if prev_slot > slot else "down"
            new["changeStamp"] = stamp
            row["_lastMove"] = stamp
        elif row["_lastMove"] and (stamp - row["_lastMove"]) > 150:
            # 150 Payloads ~ 5 s bei 30 Hz gedrosseltem Strom. Bewusst in Payloads
            # gerechnet und nicht in Sekunden: der Pfeil soll an den Datenfluss
            # gekoppelt sein, nicht an die Uhr (steht der Strom, steht auch er).
            new["changeDir"] = "none"
            row["_lastMove"] = 0
        row["_prevSlot"] = slot

        # Bestrunde-Flash: neue persoenliche Bestzeit gruen, neue Session-Bestzeit lila.
        best_lap = d.get("best_lap") or 0
        prev_bl = row["_prevBestLap"]
        if cfg["pbflash"] and not is_new and best_lap > 0 and prev_bl \
                and best_lap < prev_bl - 0.0005:
            new["flashKind"] = "p" if (fastest and d.get("name") == fastest) else "g"
            new["flashStamp"] = stamp
        if best_lap > 0:
            row["_prevBestLap"] = best_lap

        # Reifenwechsel -> das neue Icon dreht sich herein.
        compound = d.get("compound") or "?"
        if row["_prevCompound"] and compound != row["_prevCompound"]:
            new["tyreStamp"] = stamp
        row["_prevCompound"] = compound

        quali_st = shared.quali_status(d)
        sectors, classes = shared.sector_view(d, is_quali, quali_st)

        team = d.get("team") or ""
        grid = d.get("grid_position") or 0
        comeback = 0
        if cfg["comeback"] and not is_quali and grid > 0 \
                and not d.get("dnf") and not d.get("dsq"):
            comeback = grid - (d.get("position") or 0)

        new.update({
            "slot": slot,
            "position": d.get("position") or 0,
            "name": d.get("name") or "",
            "team": team,
            "teamColor": TEAM_COLORS.get(team, "#ffffff"),
            "teamLogo": TEAM_LOGOS.get(team, ""),
            "even": slot % 2 == 0,
            "isFocused": focus_index is not None and focus_index >= 0
                         and d["index"] == focus_index,
            "isFinished": bool(d.get("finished")),
            "isFastestLap": bool(fastest) and d.get("name") == fastest,
            "elimZone": elim_cut > 0 and (d.get("position") or 0) > elim_cut,
            # Track-Limits faerben die Sektoren nur auf einem echten Hotlap rot.
            "lapInvalid": is_quali and quali_st == "track" and bool(d.get("lap_invalid")),
            "gapToLeader": d.get("gap_to_leader") or 0.0,
            "gapToAhead": d.get("gap_to_ahead") or 0.0,
            "bestLap": best_lap,
            "lastLap": d.get("last_lap") or 0.0,
            "lapsDown": d.get("laps_down") or 0,
            "dnf": bool(d.get("dnf")),
            "dsq": bool(d.get("dsq")),
            "inPit": bool(d.get("in_pit")),
            "compound": compound,
            "tyreAge": d.get("tyre_age") or 0,
            "tyreIcon": compound if compound in TYRE_ICONS else "",
            "sectors": list(sectors),
            "sectorClasses": list(classes),
            "qualiStatus": quali_st,
            "drs": bool(d.get("drs")),
            "overtakeActive": bool(d.get("overtake_active")),
            "overtakeAvailable": bool(d.get("overtake_available")),
            # Strafen gibt es nur im Rennen - in der Quali bleiben die Pillen leer.
            "penalties": 0 if is_quali else (d.get("penalties") or 0),
            "penDt": 0 if is_quali else (d.get("pen_dt") or 0),
            # Bei der 3. Verwarnung gibt es die Strafe und der Zaehler beginnt von
            # vorn -> Modulo 3, genau wie in tower.js.
            "cornerWarnings": 0 if is_quali else (d.get("corner_warnings") or 0) % 3,
            "comeback": comeback,
            # Ausgeschiedene zeigen keinen Schaden mehr (sonst blinkt es ewig weiter).
            "dmgFl": 0 if (is_quali or d.get("dnf") or d.get("dsq") or not cfg["damage"])
                     else (d.get("dmg_fl") or 0),
            "dmgFr": 0 if (is_quali or d.get("dnf") or d.get("dsq") or not cfg["damage"])
                     else (d.get("dmg_fr") or 0),
            "dmgRw": 0 if (is_quali or d.get("dnf") or d.get("dsq") or not cfg["damage"])
                     else (d.get("dmg_rw") or 0),
        })

        changed = [k for k, v in new.items() if row.get(k) != v]
        row.update(new)
        # Frisch eingefuegte Zeilen brauchen kein dataChanged - beginInsertRows hat
        # QML gerade erst dazu gebracht, den Delegate mit genau diesen Werten zu bauen.
        return [] if is_new else changed

    def clear(self) -> None:
        if not self._rows:
            return
        self.beginResetModel()
        self._rows.clear()
        self._by_index.clear()
        self._miss.clear()
        self.endResetModel()
        self.countChanged.emit()


def _prop(name, type_, default):
    """Kleiner Helfer: Property + Signal, das nur bei echter Aenderung feuert.

    Von Hand waeren das pro Feld sieben Zeilen Rumpf, und SessionState hat gut
    zwanzig davon. Wichtig ist der Gleichheitstest: ohne ihn feuert jedes Feld bei
    jedem Payload und QML rechnet Bindings neu, die sich gar nicht geaendert haben.
    """
    attr = "_" + name
    signal = Signal()

    def getter(self):
        return getattr(self, attr, default)

    def setter(self, value):
        if getattr(self, attr, default) == value:
            return
        setattr(self, attr, value)
        getattr(self, name + "Changed").emit()

    return signal, Property(type_, getter, setter, notify=signal)


class _StateBase(QObject):
    """Basis fuer die Property-Objekte: setzt mehrere Felder auf einmal."""

    def _assign(self, values: dict) -> None:
        for key, value in values.items():
            setattr(self, key, value)


class SessionState(_StateBase):
    """Alles fuer die Kopfzeile des Towers - eine Zeile pro Anzeige-Element."""

    # Verbindungen
    linkedChanged, linked = _prop("linked", bool, False)          # Kontakt zum Server
    connectedChanged, connected = _prop("connected", bool, False)  # UDP-Daten vom Spiel
    holdResultChanged, holdResult = _prop("holdResult", bool, False)

    # Titelzeile
    titleChanged, title = _prop("title", str, "")
    subtitleChanged, subtitle = _prop("subtitle", str, "")
    flagTextChanged, flagText = _prop("flagText", str, "")
    flagKindChanged, flagKind = _prop("flagKind", str, "")        # "" | "sc" | "fin"

    # Spaltenkoepfe (Rennen vs. Quali)
    isQualiChanged, isQuali = _prop("isQuali", bool, False)
    boostLabelChanged, boostLabel = _prop("boostLabel", str, "DRS")
    headLeaderChanged, headLeader = _prop("headLeader", str, "LEADER")
    headIntervalChanged, headInterval = _prop("headInterval", str, "INTERVAL")
    headRightChanged, headRight = _prop("headRight", str, "DRS")

    # Rahmen-Zustaende
    scStatusChanged, scStatus = _prop("scStatus", str, "none")    # "none" | "sc" | "vsc"
    redFlagChanged, redFlag = _prop("redFlag", bool, False)

    # Quali
    poleTimeChanged, poleTime = _prop("poleTime", float, 0.0)
    elimCutChanged, elimCut = _prop("elimCut", int, 0)

    # Wetter-Ticker: Liste von {t, emoji, rain, now}
    tickerChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._ticker = []

    @Property("QVariantList", notify=tickerChanged)
    def ticker(self):
        return self._ticker

    @ticker.setter
    def ticker(self, value):
        if self._ticker == value:
            return
        self._ticker = value
        self.tickerChanged.emit()


class SettingsState(_StateBase):
    """Die Settings vom Server, soweit das QML sie braucht."""

    brandTitleChanged, brandTitle = _prop("brandTitle", str, "")
    brandAccentChanged, brandAccent = _prop("brandAccent", str, "#e10600")
    headerColorChanged, headerColor = _prop("headerColor", str, "")
    rowColorChanged, rowColor = _prop("rowColor", str, "")
    towerLogoChanged, towerLogo = _prop("towerLogo", str, "")
    towerLogoPosChanged, towerLogoPos = _prop("towerLogoPos", str, "left")
    towerLogoHChanged, towerLogoH = _prop("towerLogoH", int, 34)
    uiAlphaChanged, uiAlpha = _prop("uiAlpha", float, 1.0)
    # Staerke der Schrift-Kontur (0..1). Gegenstueck zu uiAlpha: die faerbt die
    # Flaechen ein, diese hier macht die Schrift davon unabhaengig lesbar.
    textOutlineChanged, textOutline = _prop("textOutline", float, 0.0)
    scaleChanged, scale = _prop("scale", float, 0.0)
    rowsChanged, rows = _prop("rows", int, 0)
    showTowerChanged, showTower = _prop("showTower", bool, True)
    showTickerChanged, showTicker = _prop("showTicker", bool, True)
    dmgCritChanged, dmgCrit = _prop("dmgCrit", int, 60)
    # Platz der Trackmap (tc/tr/rc/bl/bc/br). Ohne diese Zeile kam die Auswahl
    # aus /settings nie im QML an und die Karte blieb immer oben rechts.
    mapCornerChanged, mapCorner = _prop("mapCorner", str, "tr")

    # Battle-Boxen nebeneinander ("row") oder gestapelt ("column").
    battleDirChanged, battleDir = _prop("battleDir", str, "row")
    # Seite der Strafen-Pillen und ob sie bei der Zielflagge weichen.
    penSideChanged, penSide = _prop("penSide", str, "left")
    penHideFinishChanged, penHideFinish = _prop("penHideFinish", bool, True)

    # Freies Layout, je Baustein {ecke, dx, dy, z, groesse}. Leerer Eintrag = der
    # Baustein bleibt an seinem einprogrammierten Platz. Einzelne Felder duerfen
    # fehlen und fallen dann ebenfalls auf den eingebauten Wert zurueck - ein
    # Eintrag nur mit `z` kommt aus der Ebenen-Liste in /settings (siehe
    # Overlay.qml, Funktion `hat`).
    layoutChanged, layout = _prop("layout", "QVariantMap", {})

    def apply(self, cfg) -> None:
        opacity = cfg["opacity"] if cfg["opacity"] > 0 else 1.0
        self._assign({
            "brandTitle": str(cfg["brand_title"] or ""),
            "brandAccent": str(cfg["brand_accent"] or "#e10600"),
            "headerColor": str(cfg["header_color"] or ""),
            "rowColor": str(cfg["row_color"] or ""),
            "towerLogo": str(cfg["tower_logo"] or ""),
            "towerLogoPos": str(cfg["tower_logo_pos"] or "left"),
            "towerLogoH": int(cfg["tower_logo_h"] or 34),
            # Grenzen wie in applyBrand() in core.js: nach unten 0.2, nach oben 1.25
            # (darueber ist ohnehin alles deckend).
            "uiAlpha": max(0.2, min(1.25, float(opacity))),
            "textOutline": max(0.0, min(1.0, float(cfg["text_outline"] or 0))),
            "scale": float(cfg["scale"] or 0),
            "rows": int(cfg["rows"] or 0),
            "showTower": bool(cfg["tower"]),
            "showTicker": bool(cfg["ticker"]),
            "dmgCrit": int(cfg["dmgcrit"] or 60),
            "mapCorner": str(cfg["mapcorner"] or "tr"),
            "battleDir": "column" if str(cfg["battledir"]) == "column" else "row",
            "penSide": "right" if str(cfg["penside"]) == "right" else "left",
            "penHideFinish": bool(cfg["penhidefinish"]),
            "layout": dict(cfg["layout"] or {}),
        })


class RegieState(_StateBase):
    """Die manuellen Einblendungen aus /regie."""

    chartChanged, chart = _prop("chart", str, "")     # "" | "pos" | "gap" | "lap"
    champChanged, champ = _prop("champ", bool, False)
    battlesChanged, battles = _prop("battles", bool, True)
    hotlapChanged, hotlap = _prop("hotlap", bool, True)

    def apply(self, regie: dict) -> None:
        regie = regie or {}
        chart = regie.get("chart")
        self._assign({
            "chart": chart if chart and chart != "none" else "",
            "champ": bool(regie.get("champ")),
            "battles": regie.get("battles") is not False,   # Vorgabe an
            "hotlap": regie.get("hotlap") is not False,
        })
