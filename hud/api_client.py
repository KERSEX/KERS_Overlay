"""
Verbindung zum Flask-Server (main.py).

Bewusst mit QNetworkAccessManager statt requests: laeuft asynchron im
Qt-Eventloop. Ein blockierender HTTP-Aufruf wuerde sonst bei jedem Klick im
Schaltbrett kurz die Oberflaeche einfrieren - und wenn der Server nicht laeuft,
haengt das Fenster sekundenlang im Timeout.

Genutzt wird genau ein Endpunkt (war schon in main.py da):
    GET  /api/status     -> {"connected": bool, "driver_count": int}

/api/settings und /api/regie fasst das HUD bewusst NICHT an: Bausteine und alle
Einstellungen macht man in /settings und /regie im Browser. Zwei Wege zur selben
Einstellung waeren zwei Wahrheiten.
"""

import json

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtNetwork import (QNetworkAccessManager, QNetworkReply,
                               QNetworkRequest)


class ApiClient(QObject):
    """Duenner Client: fragt zyklisch, ob Server und UDP leben."""

    statusChanged = Signal(bool, bool, int)   # server_ok, udp_connected, driver_count

    STATUS_MS = 1000

    def __init__(self, base_url: str, parent=None):
        super().__init__(parent)
        self._base = base_url.rstrip("/")
        self._nam = QNetworkAccessManager(self)
        self._status_inflight = False

        self._status_timer = QTimer(self)
        self._status_timer.setInterval(self.STATUS_MS)
        self._status_timer.timeout.connect(self.refresh_status)

    # -- Basis ---------------------------------------------------------------
    @property
    def base_url(self) -> str:
        return self._base

    def set_base_url(self, url: str) -> None:
        self._base = url.rstrip("/")

    def start(self) -> None:
        """Status-Polling starten."""
        self.refresh_status()
        self._status_timer.start()

    def stop(self) -> None:
        self._status_timer.stop()

    # -- intern --------------------------------------------------------------
    def _request(self, path: str) -> QNetworkRequest:
        req = QNetworkRequest(QUrl(f"{self._base}{path}"))
        req.setAttribute(QNetworkRequest.Attribute.CacheLoadControlAttribute,
                         QNetworkRequest.CacheLoadControl.AlwaysNetwork)
        req.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json")
        return req

    def _get(self, path: str, on_json) -> None:
        reply = self._nam.get(self._request(path))
        reply.finished.connect(lambda: self._finish(reply, on_json))

    def _finish(self, reply: QNetworkReply, on_json) -> None:
        try:
            if reply.error() != QNetworkReply.NetworkError.NoError:
                on_json(None)
                return
            raw = bytes(reply.readAll().data()).decode("utf-8")
            on_json(json.loads(raw))
        except Exception:
            on_json(None)
        finally:
            reply.deleteLater()

    # -- Status --------------------------------------------------------------
    def refresh_status(self) -> None:
        if self._status_inflight:
            return
        self._status_inflight = True

        def done(data):
            self._status_inflight = False
            if data is None:
                self.statusChanged.emit(False, False, 0)
            else:
                self.statusChanged.emit(True, bool(data.get("connected")),
                                        int(data.get("driver_count", 0)))

        self._get("/api/status", done)
