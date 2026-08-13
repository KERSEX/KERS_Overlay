"""
Datenzufuhr fuers QML-Overlay: haengt am SSE-Strom des Servers (/api/stream).

Das ist dieselbe Quelle, aus der auch das Web-Overlay trinkt (static/js/core.js,
`startSSE`). Der Server bleibt damit die einzige Wahrheit - QML und die HTML-Seiten
zeigen garantiert denselben Stand, solange beide nebeneinander laufen.

Aufbau
------
Ein eigener Thread liest den Strom, weil `requests` blockiert. Fertige Payloads gehen
per Qt-Signal an die GUI - Signale ueber Threadgrenzen stellt Qt selbst in die
Ereignisschlange des Empfaengers, es braucht hier also keine eigene Sperre.

    payload(dict)      neuer Live-Zustand
    linkChanged(bool)  Kontakt zum SERVER da/weg (nicht zu verwechseln mit
                       payload["connected"] - das meint die UDP-Daten vom SPIEL)

Zwei Feinheiten, beide vom Server vorgesehen (main.py, api_stream):

    ?hz=N     begrenzt den Sendetakt. Fuers Desktop-HUD sinnvoll, weil sonst
              ~12,5 Payloads/s durchlaufen, die ohnehin niemand sieht.
    ?slim=1   schickt die fast statischen Teile (settings, final_classification,
              quali_results) nur noch bei Aenderung mit. Der Client muss den letzten
              Stand behalten - genau das macht _merge_slim() hier.

Faellt der Strom aus, wird zurueckgeschaltet auf schlichtes Polling von /api/live.
Das Web-Overlay macht es genauso, und es ist die Rueckfallebene, wenn irgendein
Virenscanner oder Proxy die offene Verbindung kappt.
"""

import json
import threading
import time

import requests
from PySide6.QtCore import QObject, Signal

# Teile des Payloads, die bei ?slim=1 nur bei Aenderung mitkommen.
# ⚠ Muss zu SLIM_KEYS in main.py passen.
SLIM_KEYS = ("settings", "final_classification", "quali_results")


class LiveFeed(QObject):
    """Liest /api/stream in einem Hintergrundthread und reicht Payloads durch."""

    payload = Signal(dict)
    linkChanged = Signal(bool)

    RECONNECT_FAST_S = 2.0     # erste Versuche: der Server kommt meist gleich
    RECONNECT_SLOW_S = 15.0    # danach: er ist offenbar wirklich aus
    RECONNECT_FAST_TRIES = 8
    POLL_INTERVAL_S = 0.2      # Rueckfallebene, wie startPolling() in core.js

    def __init__(self, base_url: str, hz: int = 30, parent=None):
        super().__init__(parent)
        self._base_url = base_url.rstrip("/")
        self._hz = int(hz)
        self._stop = threading.Event()
        self._thread = None
        self._linked = False
        self._slim_cache = {}      # zuletzt gesehener Stand der SLIM_KEYS
        # Eine wiederverwendete Session spart pro Reconnect den TCP-/Handshake-Aufbau.
        self._session = requests.Session()

    # ── Steuerung ────────────────────────────────────────────────────────────
    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="kers-feed", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        # Die offene Verbindung von aussen zumachen, sonst haengt der Thread bis zum
        # naechsten Byte im read() - beim Beenden waeren das im Zweifel Sekunden.
        try:
            self._session.close()
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None

    def set_base_url(self, base_url: str) -> None:
        """Serveradresse wechseln - der Thread verbindet beim naechsten Umlauf neu."""
        new = base_url.rstrip("/")
        if new == self._base_url:
            return
        self._base_url = new
        self._reconnect()

    def set_hz(self, hz: int) -> None:
        """Sendetakt aendern. Der Parameter steckt in der URL, also muss der Strom neu."""
        hz = int(hz)
        if hz == self._hz:
            return
        self._hz = hz
        self._reconnect()

    def _reconnect(self) -> None:
        if not (self._thread and self._thread.is_alive()):
            return
        self.stop()
        self._session = requests.Session()   # die alte ist geschlossen
        self.start()

    # ── Innenleben ───────────────────────────────────────────────────────────
    @property
    def _stream_url(self) -> str:
        return f"{self._base_url}/api/stream?hz={self._hz}&slim=1"

    def _set_linked(self, linked: bool) -> None:
        if linked != self._linked:
            self._linked = linked
            self.linkChanged.emit(linked)

    def _merge_slim(self, data: dict) -> dict:
        """Bei ?slim=1 weggelassene Teile aus dem letzten Stand ergaenzen."""
        for key in SLIM_KEYS:
            if key in data:
                self._slim_cache[key] = data[key]
            elif key in self._slim_cache:
                data[key] = self._slim_cache[key]
        return data

    def _run(self) -> None:
        tries = 0
        while not self._stop.is_set():
            got_anything = self._read_stream()
            if self._stop.is_set():
                break
            self._set_linked(False)
            # Ein Strom, der sofort wieder abreisst, ist ein anderer Fall als einer,
            # der lange lief: nur beim ERSTEN Scheitern in Folge hochzaehlen.
            tries = 0 if got_anything else tries + 1
            if tries > self.RECONNECT_FAST_TRIES:
                # SSE kommt offenbar nicht durch (Proxy, Virenscanner) -> pollen.
                self._poll_once()
                self._stop.wait(self.RECONNECT_SLOW_S if tries % 10 == 0
                                else self.POLL_INTERVAL_S)
            else:
                self._stop.wait(self.RECONNECT_FAST_S)

    def _read_stream(self) -> bool:
        """Einen SSE-Strom bis zum Abriss lesen. True, wenn Daten ankamen."""
        got = False
        try:
            # timeout=(connect, read). Der Read-Timeout muss ueber dem Heartbeat des
            # Servers liegen (der schickt mindestens 1x/s), sonst reisst eine ruhige
            # Verbindung staendig grundlos ab.
            with self._session.get(self._stream_url, stream=True,
                                   timeout=(3.0, 10.0)) as resp:
                resp.raise_for_status()
                for line in resp.iter_lines(decode_unicode=True):
                    if self._stop.is_set():
                        break
                    if not line or not line.startswith("data: "):
                        continue     # Leerzeile = Ereignisende, Kommentare ignorieren
                    try:
                        data = json.loads(line[6:])
                    except (ValueError, TypeError):
                        continue     # halbes Paket - der naechste Payload heilt es
                    got = True
                    self._set_linked(True)
                    self.payload.emit(self._merge_slim(data))
        except Exception:  # pylint: disable=broad-exception-caught
            # Verbindungsfehler sind hier der Normalfall (Server noch nicht da,
            # Server beendet, Netz weg). Der Aufrufer macht daraus einen Neuversuch.
            pass
        return got

    def _poll_once(self) -> None:
        """Rueckfallebene: ein einzelner Abruf von /api/live."""
        try:
            resp = self._session.get(f"{self._base_url}/api/live", timeout=2.0)
            resp.raise_for_status()
            self._set_linked(True)
            self.payload.emit(self._merge_slim(resp.json()))
        except Exception:  # pylint: disable=broad-exception-caught
            self._set_linked(False)
