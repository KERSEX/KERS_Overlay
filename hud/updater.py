"""
Sucht neue Fassungen auf GitHub und tauscht die EXE gegen sich selbst aus.

Ablauf, bewusst in zwei Schritten:
    check()     fragt das neueste Release ab und meldet nur, dass es etwas gibt.
    download()  laedt es - erst auf Knopfdruck. Nichts passiert ungefragt, damit
                kein Update mitten in einen Stream faellt.

Die Version der laufenden Fassung steht in static/version.txt (common.APP_VERSION),
die des Releases im Tag `KERS_SubsystemsV0.0.1`. Beide werden als ZAHLEN verglichen -
als Text waere "0.0.10" kleiner als "0.0.9".

⚠ Netzwerk laeuft ueber QNetworkAccessManager, nicht ueber requests: ein blockierender
Aufruf wuerde das Schaltbrett einfrieren, solange GitHub braucht. Gleiche Begruendung
wie in api_client.py.
"""

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtNetwork import (QNetworkAccessManager, QNetworkReply,
                               QNetworkRequest)

from common import APP_VERSION, umwelt_ohne_pyinstaller

REPO = "KERSEX/KERS_Overlay"
API_URL = f"https://api.github.com/repos/{REPO}/releases/latest"
ALLE_URL = f"https://api.github.com/repos/{REPO}/releases?per_page=100"
RELEASES_PAGE = f"https://github.com/{REPO}/releases"

# Ab hier laufen die Datenordner-Umzuege (data/). Aeltere Fassungen kennen den
# Ordner nicht, suchen neben der EXE und legen frische Vorgaben an - der WM-Stand
# und die Einstellungen wirken dann verschwunden. Zerstoert wird nichts
# (migrate_data ueberspringt, was in data/ schon liegt), aber es verwirrt.
ERSTE_MIT_DATENORDNER = (0, 1, 1)

# Nur dieses Asset wird angefasst - nicht blind das erste im Release.
ASSET_NAME = "KERS_Subsystems.exe"
# Woher heruntergeladen werden darf. GitHub leitet von github.com auf den
# Objektspeicher um; alles andere waere ein fremder Server.
ERLAUBTE_HOSTS = ("github.com", "objects.githubusercontent.com",
                  "release-assets.githubusercontent.com")
# ⚠ Der Punkt hinter dem V ist Absicht: das Release 0.1.0 heisst auf GitHub
# "KERS_SubsystemsV.0.1.0" - beim Anlegen ist ein Punkt zu viel gerutscht. Ohne
# das optionale \.? faellt genau dieses Release still aus jeder Liste, und
# niemand sieht warum. Der Tag sollte trotzdem irgendwann geradegezogen werden.
TAG_RE = re.compile(r"KERS_SubsystemsV\.?(\d+\.\d+\.\d+)")


def als_zahlen(version: str) -> tuple:
    """'0.0.10' -> (0, 0, 10). Unlesbares ergibt (0, 0, 0)."""
    try:
        return tuple(int(t) for t in version.strip().split("."))
    except (AttributeError, ValueError):
        return (0, 0, 0)


def version_aus_tag(tag: str) -> str | None:
    m = TAG_RE.search(tag or "")
    return m.group(1) if m else None


def releases_aus_json(rohtext: bytes) -> list:
    """Antwort von /releases in eine Liste umwandeln, neueste zuerst.

    Bewusst eine freie Funktion ohne Qt und ohne Netz: so laesst sich die
    Auswertung mit einer gespeicherten Antwort pruefen, ohne GitHub zu fragen.

    Uebersprungen wird, was man ohnehin nicht laden koennte: Entwuerfe, Releases
    ohne lesbare Version im Tag und solche ohne unser Asset.

    Rueckgabe je Eintrag: version, zahlen, size, digest, url, published.
    """
    daten = json.loads(rohtext.decode("utf-8"))
    liste = []
    for rel in daten:
        if rel.get("draft"):
            continue
        version = version_aus_tag(rel.get("tag_name", ""))
        if version is None:
            continue
        asset = next((a for a in rel.get("assets", [])
                      if a.get("name") == ASSET_NAME), None)
        if asset is None:
            continue
        liste.append({
            "version": version,
            "zahlen": als_zahlen(version),
            "size": int(asset.get("size") or 0),
            "digest": asset.get("digest") or "",
            "url": asset.get("browser_download_url", ""),
            "published": (rel.get("published_at") or "")[:10],
            "vorab": bool(rel.get("prerelease")),
        })
    liste.sort(key=lambda r: r["zahlen"], reverse=True)
    return liste


def altlasten_aufraeumen() -> list:
    """Reste eines Updates entfernen - erst, wenn die neue Fassung wirklich laeuft.

    Das .bak ist die Rueckfallebene des Austauschs (siehe tausch_starten): bis
    hierher konnte niemand wissen, ob die neue EXE ueberhaupt startet. Dass
    DIESER Code laeuft, ist der Beweis - Bootloader, Python und Qt sind oben,
    also darf die alte Fassung weg. Mit ihr das Helfer-Batch, falls es sich
    nicht selbst geloescht hat, und ein liegengebliebenes .new aus einem
    abgebrochenen Download.

    ⚠ Bewusst NICHT schon im Batch aufraeumen: waere das .bak dort geloescht,
    gaebe es bei einer nicht startfaehigen neuen Fassung keinen Weg zurueck.

    Rueckgabe: die Namen der entfernten Dateien (fuer die Meldung im Log).
    """
    if not getattr(sys, "frozen", False):
        return []                       # im Dev-Betrieb gibt es nichts zu tauschen
    exe = Path(sys.executable).resolve()
    entfernt = []
    for datei in (exe.with_suffix(exe.suffix + ".bak"),
                  exe.with_suffix(exe.suffix + ".new"),
                  exe.parent / "_kers_update.bat"):
        try:
            if datei.is_file():
                datei.unlink()
                entfernt.append(datei.name)
        except OSError:
            pass                        # gesperrt? Dann beim naechsten Start
    return entfernt


class Updater(QObject):
    """Sucht Updates auf GitHub und legt die neue EXE daneben."""

    # (version, groesse_bytes, veroeffentlicht)
    updateAvailable = Signal(str, int, str)
    upToDate = Signal()
    progress = Signal(int, int)          # geladen, gesamt
    ready = Signal(str)                  # Pfad der fertigen .new
    failed = Signal(str)
    releasesGeladen = Signal(list)       # alle Fassungen, neueste zuerst

    def __init__(self, parent=None):
        super().__init__(parent)
        self._nam = QNetworkAccessManager(self)
        self._reply = None
        self._file = None
        self._info = {}                  # letztes Ergebnis von check()
        self._releases = []              # letztes Ergebnis von releases_laden()

    # ── Pfade ────────────────────────────────────────────────────────────────
    @property
    def exe_path(self) -> Path | None:
        """Die laufende EXE. Im Dev-Betrieb gibt es keine."""
        if not getattr(sys, "frozen", False):
            return None
        return Path(sys.executable).resolve()

    @property
    def neue_datei(self) -> Path | None:
        exe = self.exe_path
        return exe.with_suffix(exe.suffix + ".new") if exe else None

    # ── Suchen ───────────────────────────────────────────────────────────────
    def check(self) -> None:
        req = QNetworkRequest(QUrl(API_URL))
        # GitHub weist Anfragen ohne User-Agent ab.
        req.setRawHeader(b"User-Agent", f"KERS-Subsystems/{APP_VERSION}".encode())
        req.setRawHeader(b"Accept", b"application/vnd.github+json")
        reply = self._nam.get(req)
        reply.finished.connect(lambda: self._check_fertig(reply))

    def _check_fertig(self, reply) -> None:
        try:
            if reply.error() != QNetworkReply.NetworkError.NoError:
                code = reply.attribute(
                    QNetworkRequest.Attribute.HttpStatusCodeAttribute)
                if code == 404:
                    self.failed.emit(
                        "Kein Release gefunden - ist das Repository noch oeffentlich?")
                elif code == 403:
                    self.failed.emit(
                        "GitHub blockt gerade (zu viele Anfragen). Spaeter nochmal.")
                else:
                    self.failed.emit(f"Suche fehlgeschlagen: {reply.errorString()}")
                return
            data = json.loads(bytes(reply.readAll().data()).decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            self.failed.emit(f"Antwort von GitHub unlesbar: {exc}")
            return
        finally:
            reply.deleteLater()

        version = version_aus_tag(data.get("tag_name", ""))
        if version is None:
            self.failed.emit("Keine gueltige Version im Release-Tag.")
            return

        asset = next((a for a in data.get("assets", [])
                      if a.get("name") == ASSET_NAME), None)
        if asset is None:
            self.failed.emit(f"Im Release fehlt {ASSET_NAME}.")
            return

        self._info = {
            "version": version,
            "url": asset.get("browser_download_url", ""),
            "size": int(asset.get("size") or 0),
            "digest": asset.get("digest") or "",        # z.B. "sha256:abc..."
            "published": (data.get("published_at") or "")[:10],
        }
        if als_zahlen(version) > als_zahlen(APP_VERSION):
            self.updateAvailable.emit(version, self._info["size"],
                                      self._info["published"])
        else:
            self.upToDate.emit()

    # ── Alle Fassungen ───────────────────────────────────────────────────────
    def releases_laden(self) -> None:
        """Die Liste aller Releases holen - fuer die Auswahl im Schaltbrett."""
        req = QNetworkRequest(QUrl(ALLE_URL))
        req.setRawHeader(b"User-Agent", f"KERS-Subsystems/{APP_VERSION}".encode())
        req.setRawHeader(b"Accept", b"application/vnd.github+json")
        reply = self._nam.get(req)
        reply.finished.connect(lambda: self._releases_fertig(reply))

    def _releases_fertig(self, reply) -> None:
        try:
            if reply.error() != QNetworkReply.NetworkError.NoError:
                code = reply.attribute(
                    QNetworkRequest.Attribute.HttpStatusCodeAttribute)
                if code == 403:
                    self.failed.emit(
                        "GitHub blockt gerade (zu viele Anfragen). Spaeter nochmal.")
                else:
                    self.failed.emit(f"Liste nicht abrufbar: {reply.errorString()}")
                return
            self._releases = releases_aus_json(bytes(reply.readAll().data()))
        except (ValueError, UnicodeDecodeError, KeyError) as exc:
            self.failed.emit(f"Liste von GitHub unlesbar: {exc}")
            return
        finally:
            reply.deleteLater()
        self.releasesGeladen.emit(list(self._releases))

    @property
    def releases(self) -> list:
        return list(self._releases)

    def waehlen(self, version: str) -> str:
        """Eine Fassung aus der Liste scharfstellen; danach laedt download() sie.

        Damit laeuft der Wechsel auf eine ALTE Fassung durch dieselbe gepruefte
        Kette wie ein Update - Groessen- und SHA256-Pruefung, Host-Allowlist,
        Austausch mit .bak. Nur die Quelle ist eine andere.

        Rueckgabe: leerer String = in Ordnung, sonst der Fehlertext.
        """
        gesucht = (version or "").strip().lstrip("v").strip()
        treffer = next((r for r in self._releases if r["version"] == gesucht), None)
        if treffer is None:
            return (f"Version {gesucht!r} gibt es nicht. "
                    "Verfuegbar: " + ", ".join(r["version"] for r in self._releases))
        if treffer["version"] == APP_VERSION:
            return f"Version {gesucht} laeuft bereits."
        self._info = {k: treffer[k] for k in ("version", "url", "size", "digest",
                                              "published")}
        return ""

    # ── Laden ────────────────────────────────────────────────────────────────
    def download(self) -> None:
        ziel = self.neue_datei
        if ziel is None:
            self.failed.emit("Im Dev-Betrieb gibt es nichts zu tauschen.")
            return
        url = QUrl(self._info.get("url", ""))
        if url.host() not in ERLAUBTE_HOSTS:
            # Schuetzt davor, dass eine manipulierte Antwort auf einen fremden
            # Server zeigt und wir uns dessen Datei als Programm unterschieben.
            self.failed.emit(f"Unerwartete Download-Adresse: {url.host()}")
            return
        try:
            self._file = open(ziel, "wb")
        except OSError as exc:
            self.failed.emit(f"{ziel.name} nicht anlegbar: {exc}")
            return

        req = QNetworkRequest(url)
        req.setRawHeader(b"User-Agent", f"KERS-Subsystems/{APP_VERSION}".encode())
        req.setAttribute(QNetworkRequest.Attribute.RedirectPolicyAttribute,
                         QNetworkRequest.RedirectPolicy.NoLessSafeRedirectPolicy)
        self._reply = self._nam.get(req)
        # In die Datei streamen statt zu sammeln - die EXE ist ueber 200 MB gross.
        self._reply.readyRead.connect(self._stueck)
        # ⚠ Nicht direkt an self.progress haengen: downloadProgress liefert qint64,
        # unser Signal nimmt int - PySide verweigert die Verbindung dann mit
        # "Failed to connect signal downloadProgress(qlonglong,qlonglong)" und der
        # Download bricht ab, bevor er beginnt. Das Lambda wandelt um.
        self._reply.downloadProgress.connect(
            lambda gelesen, gesamt: self.progress.emit(int(gelesen), int(gesamt)))
        self._reply.finished.connect(self._download_fertig)

    def _stueck(self) -> None:
        if self._file and self._reply:
            self._file.write(self._reply.readAll().data())

    def _download_fertig(self) -> None:
        reply, self._reply = self._reply, None
        datei, self._file = self._file, None
        if datei:
            datei.close()
        ziel = self.neue_datei
        try:
            if reply.error() != QNetworkReply.NetworkError.NoError:
                self._wegwerfen(ziel)
                self.failed.emit(f"Download abgebrochen: {reply.errorString()}")
                return
        finally:
            reply.deleteLater()

        fehler = self._pruefen(ziel)
        if fehler:
            self._wegwerfen(ziel)
            self.failed.emit(fehler)
            return
        self.ready.emit(str(ziel))

    def _pruefen(self, ziel: Path) -> str:
        """Leerer String = in Ordnung. Erst danach darf getauscht werden."""
        soll = self._info.get("size") or 0
        try:
            ist = ziel.stat().st_size
        except OSError as exc:
            return f"Heruntergeladene Datei nicht lesbar: {exc}"
        if soll and ist != soll:
            return (f"Groesse passt nicht ({ist} statt {soll} Bytes) - "
                    "der Download ist unvollstaendig.")
        digest = self._info.get("digest") or ""
        if digest.startswith("sha256:"):
            h = hashlib.sha256()
            with open(ziel, "rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    h.update(block)
            if h.hexdigest() != digest.split(":", 1)[1]:
                return "Pruefsumme stimmt nicht - Datei verwerfen."
        return ""

    @staticmethod
    def _wegwerfen(ziel) -> None:
        try:
            if ziel:
                Path(ziel).unlink(missing_ok=True)
        except OSError:
            pass

    # ── Tauschen ─────────────────────────────────────────────────────────────
    def tausch_starten(self) -> str:
        """Helfer starten, der nach dem Beenden die EXE ersetzt.

        Eine laufende EXE kann sich unter Windows nicht selbst ueberschreiben.
        Deshalb ein kleines Batch: es wartet, bis wir weg sind, schiebt die alte
        Fassung als .bak beiseite, zieht die neue an ihren Platz und startet sie.
        Bleibt etwas liegen, ist die alte einen Umbenennen entfernt - und beim
        naechsten Start wird der Tausch erneut versucht.

        ⚠ Gewartet wird auf ZWEI Prozesse. Eine onefile-EXE besteht aus dem
        Bootloader, der sich nach %TEMP%\\_MEInnnnnn auspackt, und dem Kind
        darin, in dem Python laeuft - `os.getpid()` ist das KIND. Beim Beenden
        raeumt der Bootloader seinen _MEI-Ordner wieder weg. Wartete das Batch
        nur auf das Kind, liefe die neue Fassung genau in dieses Aufraeumen
        hinein und meldete beim Start:
            Failed to load Python DLL '...\\_MEInnnnnn\\python313.dll'
        Deshalb zusaetzlich `os.getppid()` - der Bootloader ueber uns.

        Die Warteschleife ist begrenzt: laeuft die EXE ausnahmsweise nicht als
        onefile, ist der Elternprozess irgendein Fenster, das nie endet.

        Rueckgabe: leerer String = angestossen, sonst der Fehlertext.
        """
        exe, neu = self.exe_path, self.neue_datei
        if exe is None or neu is None or not neu.is_file():
            return "Es liegt keine heruntergeladene Fassung bereit."

        pid, ppid = os.getpid(), os.getppid()
        bak = exe.with_suffix(exe.suffix + ".bak")
        skript = exe.parent / "_kers_update.bat"
        inhalt = (
            "@echo off\r\n"
            "chcp 65001 >nul\r\n"
            # Zweiter Riegel gegen den DLL-Fehler: die Popen-Umgebung ist zwar
            # schon gesaeubert, aber hier steht es noch einmal schwarz auf weiss.
            # Bleiben diese Variablen stehen, haelt sich die neue Fassung fuer
            # bereits ausgepackt und sucht ihre python313.dll im _MEI-Ordner der
            # alten. Siehe common.umwelt_ohne_pyinstaller.
            'set "_PYI_APPLICATION_HOME_DIR="\r\n'
            'set "_PYI_ARCHIVE_FILE="\r\n'
            'set "_PYI_PARENT_PROCESS_LEVEL="\r\n'
            'set "_MEIPASS2="\r\n'
            "set /a versuche=0\r\n"
            ":warten\r\n"
            "set /a versuche+=1\r\n"
            "if %versuche% gtr 40 goto tauschen\r\n"
            f'tasklist /FI "PID eq {pid}" 2>nul | find "{pid}" >nul\r\n'
            "if not errorlevel 1 goto nochmal\r\n"
            f'tasklist /FI "PID eq {ppid}" 2>nul | find "{ppid}" >nul\r\n'
            "if not errorlevel 1 goto nochmal\r\n"
            "goto tauschen\r\n"
            ":nochmal\r\n"
            "timeout /t 1 /nobreak >nul\r\n"
            "goto warten\r\n"
            ":tauschen\r\n"
            f'if exist "{bak}" del /q "{bak}"\r\n'
            f'move /y "{exe}" "{bak}" >nul\r\n'
            f'move /y "{neu}" "{exe}" >nul\r\n'
            f'if errorlevel 1 move /y "{bak}" "{exe}" >nul\r\n'
            # Kurz durchatmen lassen, bevor die neue Fassung sich auspackt.
            "timeout /t 2 /nobreak >nul\r\n"
            f'start "" "{exe}"\r\n'
            'del "%~f0"\r\n'
        )
        try:
            # ⚠ CRLF und keine BOM: cmd.exe verschluckt bei reinen LF-Zeilenenden
            # Zeichen am Zeilenanfang - das hat hier schon Builds gekostet.
            skript.write_bytes(inhalt.encode("utf-8"))
            # ⚠ env ist hier das Entscheidende, nicht der Rest. Ohne die
            # Saeuberung erbt das Batch unsere _PYI_-Variablen, gibt sie an
            # "start" weiter, und der Bootloader der neuen Fassung haelt sich
            # fuer bereits ausgepackt - er sucht seine python313.dll dann im
            # _MEI-Ordner der ALTEN Fassung, den es nicht mehr gibt. Begruendung
            # in voller Laenge bei common.umwelt_ohne_pyinstaller.
            subprocess.Popen(["cmd", "/c", str(skript)],
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                             cwd=str(exe.parent),
                             env=umwelt_ohne_pyinstaller())
        except OSError as exc:
            return f"Austausch nicht startbar: {exc}"
        return ""
