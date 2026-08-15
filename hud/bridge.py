"""
Das Bindeglied zwischen Server und QML-Szene.

Der Weg eines Payloads:

    /api/stream  ->  LiveFeed (feed.py, eigener Thread)
                 ->  OverlayBridge._on_payload  (GUI-Thread)
                     |- Config.apply          Settings vom Server uebernehmen
                     |- SharedState.maybe_hold_quali
                     |- SharedState.derive    gemeinsame Buchhaltung
                     |- SessionState          Kopfzeile
                     `- DriverModel.update    Fahrerzeilen
                 ->  QML liest die Properties und zeichnet

In QML liegt das Objekt unter dem Namen `KERS`:

    KERS.session.title
    KERS.drivers        (Modell fuer Repeater/ListView)
    KERS.settings.uiAlpha

Die Aufteilung der Zustaende in mehrere kleine Objekte ist Absicht: aendert sich der
Session-Titel, sollen nicht die Bindings der Settings mit neu rechnen.
"""

import json
import time

from PySide6.QtCore import QObject, Property, QUrl, Signal, Slot
from PySide6.QtNetwork import (QNetworkAccessManager, QNetworkReply,
                               QNetworkRequest)

from derive import Config, SharedState
from extras import Championship, Charts, Trackmap
from feed import LiveFeed
from models import DriverModel, RegieState, SessionState, SettingsState
from parts import (Battles, DangerZone, FastestLap, Hotlaps, LowerThird,
                   MessageBanner, Onboard, PitCards, PitProjection, RaceControl,
                   StartLights, Undercut)

# Wie lange der rote Rahmen nach einer roten Flagge stehen bleibt (tower.js: 15 s).
RED_FLAG_MS = 15000


class OverlayBridge(QObject):
    """Alles, was die QML-Szene ueber den Rennstand wissen muss."""

    # Ein Zaehler, der bei jedem Payload hochgeht. QML kann sich darauf haengen,
    # wenn etwas "bei jedem neuen Stand" passieren soll, ohne ein konkretes Feld
    # zu beobachten.
    tickChanged = Signal()

    def __init__(self, base_url: str, hz: int = 30, overrides: dict | None = None,
                 demo: str = "", parent=None):
        super().__init__(parent)
        self._cfg = Config(overrides)
        self._shared = SharedState(self._cfg)
        # Fuer den Rueckweg zum Server (Layout speichern). Parent ist bewusst
        # self: ein QObject ohne Parent raeumt der Python-GC weg, waehrend seine
        # Anfrage noch laeuft - das hat hier schon einen Absturz gekostet.
        self._base_url = base_url.rstrip("/")
        self._nam = QNetworkAccessManager(self)

        self._drivers = DriverModel(self)
        self._session = SessionState(self)
        self._settings = SettingsState(self)
        self._regie = RegieState(self)
        self._tick = 0

        # Die einzelnen Bausteine. Jeder haelt seinen eigenen abgeleiteten Zustand,
        # genau wie die zugehoerige Datei in static/parts/ es im Browser tut.
        self._banner = MessageBanner(self)
        self._race_control = RaceControl(self._banner)
        self._undercut = Undercut(self._banner)
        self._battles = Battles(self._cfg, self)
        self._hotlaps = Hotlaps(self._shared, self._cfg, self)
        self._onboard = Onboard(self._shared, self._cfg, self)
        self._lower_third = LowerThird(self._shared, self._cfg, self)
        self._pit = PitCards(self)
        self._lights = StartLights(self)
        self._fastest_lap = FastestLap(self._cfg, self)
        self._danger = DangerZone(self)
        self._pit_proj = PitProjection(self)
        self._trackmap = Trackmap(base_url, self._cfg, self)
        self._charts = Charts(base_url, self._shared, self)
        self._champ = Championship(base_url, self)

        # Ergebnis stehen lassen (tower.js: everConn / lastConnTs)
        self._ever_connected = False
        self._last_conn_ms = 0.0
        # Rote Flagge: eigene Sichtung des race_control-Feeds (tower.js rfLastRcId)
        self._red_flag_until = 0.0
        self._rf_last_rc_id = 0

        # Der Demo-Feed hat dieselbe Schnittstelle wie der echte (payload/linkChanged/
        # start/stop) - der Rest der Bruecke merkt keinen Unterschied.
        self._hz = hz
        self._demo_dauerhaft = bool(demo)      # per --demo gestartet
        self._demo_an = bool(demo)
        if demo:
            from demo import DemoFeed
            self._feed = DemoFeed(hz, quali=(demo == "quali"), parent=self)
        else:
            self._feed = LiveFeed(base_url, hz, self)
        self._feed.payload.connect(self._on_payload)
        self._feed.linkChanged.connect(self._on_link)

    # ── Fuer QML ─────────────────────────────────────────────────────────────
    @Property(QObject, constant=True)
    def drivers(self):
        return self._drivers

    @Property(QObject, constant=True)
    def session(self):
        return self._session

    @Property(QObject, constant=True)
    def settings(self):
        return self._settings

    @Property(QObject, constant=True)
    def regie(self):
        return self._regie

    @Property(int, notify=tickChanged)
    def tick(self):
        return self._tick

    # ── Die Bausteine, wie QML sie sieht ─────────────────────────────────────
    @Property(QObject, constant=True)
    def banner(self):
        return self._banner

    @Property(QObject, constant=True)
    def battles(self):
        return self._battles

    @Property(QObject, constant=True)
    def hotlaps(self):
        return self._hotlaps

    @Property(QObject, constant=True)
    def onboard(self):
        return self._onboard

    @Property(QObject, constant=True)
    def lowerThird(self):
        return self._lower_third

    @Property(QObject, constant=True)
    def pit(self):
        return self._pit

    @Property(QObject, constant=True)
    def lights(self):
        return self._lights

    @Property(QObject, constant=True)
    def fastestLap(self):
        return self._fastest_lap

    @Property(QObject, constant=True)
    def danger(self):
        return self._danger

    @Property(QObject, constant=True)
    def pitProjection(self):
        return self._pit_proj

    @Property(QObject, constant=True)
    def trackmap(self):
        return self._trackmap

    @Property(QObject, constant=True)
    def charts(self):
        return self._charts

    @Property(QObject, constant=True)
    def championship(self):
        return self._champ

    # ── Steuerung ────────────────────────────────────────────────────────────
    def start(self) -> None:
        self._feed.start()
        # Die Streckenkontur haengt nicht am SSE-Strom. Im Demo-Modus liefert sie
        # der Feed selbst, sonst wird /api/track gepollt, bis der Server die
        # Strecke fertig gelernt hat.
        track = getattr(self._feed, "track_points", None)
        if track is not None:
            self._trackmap.load_points(track())
            # Denselben Grund hat der WM-Stand: /api/championship gibt es ohne
            # Server nicht, sonst bliebe das Panel im Demo-Modus leer.
            self._champ.set_demo_data(self._feed.championship())
        else:
            self._trackmap.start()

    def stop(self) -> None:
        self._feed.stop()
        self._trackmap.stop()

    def set_base_url(self, base_url: str) -> None:
        self._base_url = base_url.rstrip("/")
        self._feed.set_base_url(base_url)
        for part in (self._trackmap, self._charts, self._champ):
            part.set_base_url(base_url)

    def set_hz(self, hz: int) -> None:
        self._feed.set_hz(hz)

    @Slot(str, "QVariant")
    def override(self, key: str, value) -> None:
        """Eine Einstellung lokal ueberschreiben (Gegenstueck zu den URL-Parametern
        der Web-Bausteine). `undefined`/None gibt sie an den Server zurueck."""
        self._cfg.set_override(key, value)
        self._settings.apply(self._cfg)

    # ── Vorschau-Daten fuers Layout-Bearbeiten ───────────────────────────────
    def set_vorschau(self, on: bool) -> None:
        """Waehrend des Layout-Bearbeitens erfundene Daten einspeisen.

        Ohne laufendes Rennen ist die Szene leer - man saehe nichts zum
        Verschieben. Der Demo-Feed hat dieselbe Schnittstelle wie der echte
        (payload/linkChanged/start/stop), der Rest der Bruecke merkt den Tausch
        also nicht.

        ⚠ Wer das HUD mit --demo gestartet hat, bekommt hier gar nichts: dort
        laeuft die Demo ohnehin, und ein Tausch wuerde sie nur neu starten.
        """
        on = bool(on)
        if self._demo_dauerhaft or on == self._demo_an:
            return
        self._feed.stop()
        try:
            self._feed.payload.disconnect(self._on_payload)
            self._feed.linkChanged.disconnect(self._on_link)
        except (RuntimeError, TypeError):
            pass
        self._feed.deleteLater()

        if on:
            from demo import DemoFeed
            self._feed = DemoFeed(self._hz, quali=False, vorschau=True, parent=self)
        else:
            self._feed = LiveFeed(self._base_url, self._hz, self)
        self._feed.payload.connect(self._on_payload)
        self._feed.linkChanged.connect(self._on_link)
        self._demo_an = on
        self._feed.start()

    # ── Layout zurueckschreiben ──────────────────────────────────────────────
    @staticmethod
    def _als_dict(wert) -> dict:
        """Ein Objekt aus QML in ein echtes Dictionary umwandeln.

        ⚠ Ein in QML gebautes JS-Objekt kommt trotz @Slot("QVariant") als
        PySide6.QtQml.QJSValue an, NICHT als dict - `dict(...)` scheitert dann mit
        "'QJSValue' object is not iterable" und das HUD stolpert. toVariant()
        wandelt um, und zwar auch die verschachtelten Eintraege. Geprueft wird per
        hasattr statt per Import, damit bridge.py nicht extra QtQml braucht.
        """
        if hasattr(wert, "toVariant"):
            wert = wert.toVariant()
        return dict(wert or {})

    @Slot("QVariant")
    def layoutLive(self, layout) -> None:
        """Waehrend des Ziehens: nur lokal, damit der Baustein der Maus folgt.

        Lokale Ueberschreibungen gewinnen gegen den Server (Config.apply), der
        laufende Datenstrom kann den Baustein also nicht zurueckspringen lassen.
        """
        self.override("layout", self._als_dict(layout))

    @Slot("QVariant")
    def layoutSpeichern(self, layout) -> None:
        """Nach dem Loslassen: an den Server schicken und danach die lokale
        Ueberschreibung wieder aufgeben.

        ⚠ Die Reihenfolge ist wichtig. Gaebe man die Ueberschreibung sofort auf,
        gaelte bis zur Antwort wieder der ALTE Serverwert und der Baustein
        spraenge kurz zurueck. Deshalb: Ueberschreibung stehen lassen, senden,
        und erst in der Antwort - die den kompletten neuen Stand enthaelt -
        aufgeben und den Stand direkt uebernehmen. Damit gibt es keine Luecke.

        Gesendet wird ueber QNetworkAccessManager statt requests: ein
        blockierender Aufruf wuerde die Overlay-Schleife anhalten (gleiche
        Begruendung wie in api_client.py und updater.py).
        """
        daten = self._als_dict(layout)
        self.override("layout", daten)

        url = QUrl(f"{self._base_url}/api/settings")
        req = QNetworkRequest(url)
        req.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader,
                      "application/json")
        reply = self._nam.post(req, json.dumps({"layout": daten}).encode("utf-8"))
        reply.finished.connect(lambda: self._layout_gesendet(reply))

    def _layout_gesendet(self, reply) -> None:
        try:
            if reply.error() != QNetworkReply.NetworkError.NoError:
                # Nicht angekommen: die Ueberschreibung BLEIBT stehen, damit die
                # Verschiebung wenigstens bis zum Neustart haelt.
                print(f"[HUD] Layout nicht gespeichert: {reply.errorString()}")
                return
            antwort = json.loads(bytes(reply.readAll().data()).decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            print(f"[HUD] Antwort auf das Layout unlesbar: {exc}")
            return
        finally:
            reply.deleteLater()
        self._cfg.set_override("layout", None)     # ab jetzt gilt der Server
        self._cfg.apply(antwort)
        self._settings.apply(self._cfg)

    # ── Payload verarbeiten ──────────────────────────────────────────────────
    def _on_link(self, linked: bool) -> None:
        self._session.linked = linked

    def _on_payload(self, data: dict) -> None:
        now = time.monotonic() * 1000.0

        self._cfg.apply(data.get("settings"))
        self._settings.apply(self._cfg)
        self._regie.apply(data.get("regie"))

        data = self._shared.maybe_hold_quali(data)
        drivers, _sc_changed = self._shared.derive(data)

        session = data.get("session") or {}
        connected = bool(data.get("connected"))
        if connected:
            self._ever_connected = True
            self._last_conn_ms = now

        self._update_red_flag(data, now)
        self._update_session(session, data, connected, now, drivers)
        self._drivers.update(drivers, self._shared, session,
                             data.get("focus_index"), self._cfg)
        self._update_parts(data, session, drivers, connected, _sc_changed)

        self._tick += 1
        self.tickChanged.emit()

    def _update_parts(self, data, session, drivers, connected, sc_changed) -> None:
        """Alle Bausteine ausser dem Tower.

        Die Bedingungen sind dieselben wie in den jeweiligen JS-Dateien - dort
        stehen sie am `KERS.onData`-Hook ganz unten, hier gebuendelt an einer Stelle.
        """
        cfg = self._cfg
        wall_ms = time.time() * 1000.0
        is_quali = bool(session.get("is_quali"))
        lap = session.get("current_lap") or 0
        focus = data.get("focus_index")

        # Meldungen: Rennleitung und Undercut teilen sich denselben Banner - genau
        # wie im Gesamt-Overlay, wo es nur ein #race-msg gibt.
        self._banner.set_enabled(bool(cfg["msgs"]))
        self._race_control.update(data)
        if sc_changed:
            self._race_control.on_safety_car(sc_changed[0], sc_changed[1])
        self._undercut.update(drivers, session, connected, bool(cfg["undercut"]))

        # Battle-Boxen: Regie-Schalter, Setting, verbunden, kein Quali, ab Runde 3.
        self._battles.update(drivers, data, session,
                             self._regie.battles and bool(cfg["battles"])
                             and connected and not is_quali and lap >= 3)

        # Hotlap-Boxen: Regie-Schalter, verbunden, Quali, kein eingefrorenes Ergebnis.
        # focus, damit der beobachtete Fahrer nach links rueckt, wenn er selbst
        # eine fliegende Runde faehrt.
        self._hotlaps.update(drivers, focus,
                             self._regie.hotlap and connected and is_quali
                             and not session.get("_quali_result"))

        self._onboard.update(drivers, focus, connected, session, bool(cfg["onboard"]))
        self._lower_third.update(drivers, focus, session, connected,
                                 bool(cfg["lowerthird"]))
        self._pit.update(drivers, session, connected)
        self._lights.update(data.get("start_lights"), connected, bool(cfg["lights"]))
        self._fastest_lap.update(session, connected, bool(cfg["flbanner"]))
        self._danger.update(drivers, session, connected, bool(cfg["danger"]))
        self._pit_proj.update(drivers, focus, session, connected, bool(cfg["pitproj"]))
        self._trackmap.update(drivers, focus, connected, session,
                              bool(cfg["map"]), wall_ms)
        self._charts.update(self._regie.chart, wall_ms)
        self._champ.update(self._regie.champ, wall_ms)

    def _update_red_flag(self, data: dict, now: float) -> None:
        """Rote Flagge aus dem race_control-Feed lesen (tower.js, gleiche Logik).

        Der Zaehler wird zurueckgesetzt, wenn die neueste Meldung eine kleinere Id hat
        als die zuletzt gesehene - dann hat der Server neu gestartet und faengt wieder
        bei 1 an.
        """
        rc = data.get("race_control") or []
        if rc and rc[-1].get("id", 0) < self._rf_last_rc_id:
            self._rf_last_rc_id = 0
        for msg in rc:
            if msg.get("id", 0) <= self._rf_last_rc_id:
                continue
            self._rf_last_rc_id = msg["id"]
            if msg.get("type") == "redflag":
                self._red_flag_until = now + RED_FLAG_MS

    def _update_session(self, session: dict, data: dict, connected: bool,
                        now: float, drivers) -> None:
        """Die Kopfzeile - Portierung des oberen Teils von renderTower()."""
        st = self._session
        is_quali = bool(session.get("is_quali"))

        # F1 26 hat statt DRS den "Overtake Mode" -> Spaltenkopf heisst dann MOM.
        is_f126 = session.get("formula") == 13 or "26" in str(session.get("formula_name") or "")
        boost = "MOM" if is_f126 else "DRS"

        if is_quali:
            type_name = session.get("type_name") or ""
            title = type_name.upper() if type_name and type_name != "Unbekannt" else st.title
            left = session.get("time_left") or 0
            subtitle = f"{int(left // 60)}:{int(left % 60):02d}" if left > 0 else ""
        else:
            # Im Rennen steht nur "Runde X / Y" - kein Wort "RENNEN".
            title = ""
            total = session.get("total_laps") or 0
            subtitle = f"Runde {session.get('current_lap', 0)} / {total}" if total > 0 else ""

        sc_status = session.get("safety_car_status", "none") if connected else "none"
        final_lap = (not is_quali and (session.get("total_laps") or 0) > 0
                     and (session.get("current_lap") or 0) >= session["total_laps"])
        if session.get("_quali_result"):
            flag_text, flag_kind = "🏁 Ergebnis", "fin"
        elif sc_status == "sc":
            flag_text, flag_kind = "Safety Car", "sc"
        elif sc_status == "vsc":
            flag_text, flag_kind = "Virtual SC", "sc"
        elif connected and final_lap:
            flag_text, flag_kind = "🏁 Letzte Runde", "fin"
        else:
            flag_text, flag_kind = "", ""

        # Quali: Pole-Zeit und Elimination-Schnitt
        pole, elim_cut = 0.0, 0
        if is_quali:
            bests = [d.get("best_lap") or 0 for d in drivers]
            bests = [b for b in bests if b > 0]
            pole = min(bests) if bests else 0.0
            elim_cut = 16 if session.get("type") == 5 else 10 if session.get("type") == 6 else 0

        # "Warte auf Telemetrie" nur, wenn NOCH NIE Daten kamen oder das letzte
        # Ergebnis laenger als `holds` her ist.
        hold = (self._ever_connected and not connected
                and (now - self._last_conn_ms) / 1000.0 < (self._cfg["holds"] or 300))

        st._assign({
            "connected": connected,
            "holdResult": hold,
            "isQuali": is_quali,
            "title": title,
            "subtitle": subtitle,
            "flagText": flag_text,
            "flagKind": flag_kind,
            "boostLabel": boost,
            "headLeader": "BEST" if is_quali else "LEADER",
            "headInterval": "GAP" if is_quali else "INTERVAL",
            "headRight": "STATUS" if is_quali else boost,
            "scStatus": sc_status,
            "redFlag": connected and now < self._red_flag_until,
            "poleTime": float(pole),
            "elimCut": elim_cut,
            "ticker": _ticker_chips(session),
        })


def _ticker_chips(session: dict) -> list:
    """Wetter-Chips fuer den Ticker im Tower-Kopf (tower.js buildTickerItems).

    Jetzt-Wert plus bis zu vier Vorhersagen. Gibt es kein Wetter, bleibt die Liste
    leer und das QML zeigt stattdessen den Ersatztext.
    """
    if not session.get("weather_name"):
        return []
    chips = [{"label": "Jetzt", "emoji": session.get("weather_emoji") or "",
              "rain": int(session.get("weather_rain") or 0), "now": True}]
    for fc in (session.get("forecast") or [])[:4]:
        chips.append({"label": f"+{fc.get('t', 0)} Min", "emoji": fc.get("emoji") or "",
                      "rain": int(fc.get("rain") or 0), "now": False})
    return chips
