"""
KERS Subsystems - das kleine Schaltbrett fuer den Desktop.

Bewusst NUR das, was es sonst nirgends gibt:

    * den Server (main.py) starten
    * das HUD-Fenster an/aus, auf den Bildschirm legen, sperren, platzieren

Alles, was Konfiguration ist - Bausteine an/aus, Regler, Branding, Presets,
Trackmap, manuelle Einblendungen - bleibt da, wo es hingehoert und schon gut ist:

    /regie      manuelle Einblendungen (Charts, Strategie, WM-Stand)
    /settings   die komplette Konfiguration inkl. aller Bausteine

Beide sind von hier aus einen Knopfdruck entfernt. Nichts davon wird hier
nachgebaut - es gaebe sonst zwei Wahrheiten fuer dieselbe Einstellung.

Eigenes Fenster und nicht Teil des HUDs: sobald das HUD gesperrt ist, gehen
Klicks durch es hindurch - darin liesse sich also nichts mehr bedienen.
"""

import hashlib
import shutil
import subprocess
import sys
import webbrowser
from pathlib import Path
from urllib.parse import urlparse

from PySide6.QtCore import QEvent, Qt, QTimer
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QCompleter,
                               QFrame, QGridLayout, QGroupBox, QHBoxLayout,
                               QLabel, QMessageBox, QPushButton, QSpinBox,
                               QVBoxLayout, QWidget)

from api_client import ApiClient
from common import (APP_VERSION, DATA_DIR, HZ_CHOICES, PAGES, RENDERERS,
                    make_icon, umwelt_ohne_pyinstaller)
from updater import ERSTE_MIT_DATENORDNER, Updater, als_zahlen

# ⚠ overlay_window (WebEngine) wird hier BEWUSST nicht importiert. Der Import zog
# frueher nur fuer eine Typangabe das komplette QtWebEngine mit hoch - auch dann,
# wenn das HUD auf QML laeuft und Chromium ueberhaupt nicht braucht. Das Schaltbrett
# spricht ohnehin nur ueber die gemeinsame Schnittstelle mit dem Fenster
# (set_locked, apply_geometry, hudGeometryChanged, ...), die beide Renderer haben.

# Ordner, in dem der Server liegt und arbeitet.
#   Dev-Betrieb: eine Ebene ueber hud/ - dort stehen main.py, run.bat und venv/.
#   Als EXE:     der Unterordner data neben KERS_Subsystems.exe (DATA_DIR).
#
# ⚠ Dass main.exe IM Datenordner liegt, ist Absicht: main.py leitet seine Pfade aus
# dem eigenen Standort ab (BASE_DIR = dirname(sys.executable)). Dadurch landen
# overlay_settings.json, championship.json, presets.json und recordings/ von selbst
# im richtigen Ordner - ohne eine einzige Pfadaenderung in main.py.
if getattr(sys, "frozen", False):
    ROOT = DATA_DIR
else:
    ROOT = Path(__file__).resolve().parent.parent


SERVER_BUSY_HINT = (
    "Neben dem Programm liegt eine main.exe aus einem aelteren Build, und sie laesst "
    "sich nicht ersetzen - der Server laeuft offenbar noch.\n\n"
    "Bitte zuerst auf \"Stop Server\" klicken (oder das Konsolenfenster des Servers "
    "schliessen) und dann erneut starten."
)


def _payload_source() -> Path | None:
    """Die in DIESER EXE eingebettete main.exe. Im Dev-Betrieb gibt es sie nicht."""
    if not getattr(sys, "frozen", False):
        return None
    p = Path(sys._MEIPASS) / "payload" / "main.exe"    # noqa: SLF001
    return p if p.is_file() else None


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def _same_content(a: Path, b: Path) -> bool:
    """Gleiche Groesse UND gleicher Inhalt.

    Die Groesse allein waere zu wacklig - zwei Builds koennen gleich gross sein,
    ohne gleich zu sein. Gehasht wird deshalb nur, wenn die Groesse schon passt;
    das kostet bei ~18 MB wenige Millisekunden und nur beim Klick auf
    "Start Server".
    """
    try:
        if a.stat().st_size != b.stat().st_size:
            return False
        return _sha256(a) == _sha256(b)
    except OSError:
        return False


def _ensure_server_payload() -> tuple[Path | None, str]:
    """main.exe neben dieser EXE sicherstellen - und zwar die AKTUELLE.

    KERS_Subsystems.exe traegt main.exe (+ dessen Config-JSONs) als eingebettete
    Kopie in sich (--add-binary/--add-data, siehe build.bat), damit sich nur EINE
    Datei verteilen laesst. Beim "Start Server" wird main.exe von dort daneben
    entpackt - ab da laeuft sie als eigener, stabiler Prozess (nicht aus dem
    Pyinstaller-Temp-Ordner, der beim Beenden wieder verschwindet).

    ⚠ Frueher wurde eine schon vorhandene main.exe einfach genommen, egal wie alt.
    Nach einem Rebuild testete man damit unbemerkt gegen den ALTEN Server, und die
    Datei musste von Hand geloescht werden. Jetzt wird der Inhalt verglichen und
    eine veraltete ersetzt.

    Rueckgabe: (Pfad, Fehlertext). Fehlertext leer heisst: alles in Ordnung.
    """
    target = ROOT / "main.exe"
    bundled = _payload_source()

    if bundled is None:
        # Dev-Betrieb oder ohne Payload gebaut: nehmen, was daliegt.
        return (target if target.is_file() else None), ""

    if target.is_file() and _same_content(target, bundled):
        return target, ""

    war_da = target.is_file()
    try:
        shutil.copy2(bundled, target)
    except PermissionError:
        if war_da:
            # Die alte laeuft noch -> ersetzen unmoeglich. Bewusst NICHT die alte
            # zurueckgeben: sonst startet stillschweigend wieder der alte Server.
            return None, SERVER_BUSY_HINT
        return None, f"main.exe laesst sich nicht anlegen (kein Zugriff auf {ROOT})."
    except OSError as e:
        return None, f"main.exe liess sich nicht entpacken: {e}"

    # Seit 0.1.1 liegen KEINE *.json mehr im Payload: Einstellungen, WM-Stand und
    # Presets sind persoenliche Daten und haben in einer verteilten EXE nichts zu
    # suchen. main.py legt fehlende Dateien beim ersten Lauf selbst an. Die Schleife
    # bleibt als Rueckfallebene fuer aeltere Builds - und kopiert nur, was fehlt.
    for f in bundled.parent.glob("*.json"):
        dest = ROOT / f.name
        if not dest.exists():
            try:
                shutil.copy2(f, dest)
            except OSError:
                pass
    return target, ""


# ── Prozess-Werkzeug ──────────────────────────────────────────────────────────
# main.py hat keinen Endpunkt zum Herunterfahren, also muss der Prozess beendet
# werden. Einen selbst gestarteten kennen wir; einen fremden (run.bat, eigenes
# Terminal) suchen wir ueber den Port - und fragen vorher nach.

def _run_hidden(args: list) -> str:
    """Konsolen-Werkzeug aufrufen, ohne dass ein Fenster aufblitzt."""
    try:
        done = subprocess.run(
            args, capture_output=True, text=True, errors="replace", timeout=10,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return done.stdout or ""
    except Exception:  # pylint: disable=broad-exception-caught
        return ""


def _find_listener_pid(port: int):
    """PID des Prozesses, der auf dem Port lauscht (oder None).

    ⚠ Die Statusspalte wird BEWUSST nicht ausgewertet: netstat ist uebersetzt,
    auf einem deutschen Windows steht dort "ABHOEREN" statt "LISTENING". Ein
    lauschender Socket ist stattdessen daran zu erkennen, dass die Gegenstelle
    Port 0 hat (0.0.0.0:0 bzw. [::]:0) - das ist in jeder Sprache gleich.
    Bestehende Verbindungen auf denselben Port haben dort einen echten Port.
    """
    for line in _run_hidden(["netstat", "-ano", "-p", "TCP"]).splitlines():
        parts = line.split()
        # Format:  TCP    0.0.0.0:5100    0.0.0.0:0    <Status>    12345
        if len(parts) < 5 or parts[0].upper() != "TCP":
            continue
        if parts[1].rsplit(":", 1)[-1] != str(port):
            continue
        if parts[2].rsplit(":", 1)[-1] != "0":
            continue
        try:
            return int(parts[-1])
        except ValueError:
            continue
    return None


def _process_name(pid: int) -> str:
    out = _run_hidden(["tasklist", "/FI", f"PID eq {pid}", "/NH", "/FO", "CSV"])
    # "python.exe","12345","Console","1","40.000 K"
    return out.split('"')[1] if out.startswith('"') else ""


def _kill_tree(pid: int) -> None:
    """Prozess samt Kindern beenden (das Konsolenfenster haengt mit dran)."""
    _run_hidden(["taskkill", "/PID", str(pid), "/T", "/F"])

STYLE = """
QWidget        { background: #0e0e13; color: #ffffff; font-family: 'Segoe UI'; font-size: 12px; }
QGroupBox      { border: 1px solid rgba(255,255,255,0.09); border-radius: 8px;
                 margin-top: 14px; padding: 12px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px;
                 color: #e10600; font-weight: bold; letter-spacing: 1px; }
QCheckBox      { padding: 2px 0; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 3px;
                 border: 1px solid rgba(255,255,255,0.25); background: #1b1b22; }
QCheckBox::indicator:checked { background: #e10600; border-color: #e10600; }
QPushButton    { background: #1b1b22; border: 1px solid rgba(255,255,255,0.12);
                 border-radius: 6px; padding: 6px 10px; }
QPushButton:hover  { background: #27272f; }
QPushButton:pressed { background: #e10600; }
QPushButton:disabled { background: #14141a; color: #6a6a78;
                 border-color: rgba(255,255,255,0.06); }
QComboBox, QSpinBox { background: #1b1b22; border: 1px solid rgba(255,255,255,0.12);
                 border-radius: 6px; padding: 4px 6px; }
QComboBox QAbstractItemView { background: #1b1b22; selection-background-color: #e10600; }
QLabel#hint    { color: #a6a6b4; }
QLabel#head    { font-size: 15px; font-weight: bold; letter-spacing: 2px; }
"""

# Der grosse Hauptschalter faellt bewusst aus dem restlichen Stil heraus -
# er ist das Erste, was man im Fenster sieht.
POWER_ON = """
QPushButton { background: #14361f; border: 1px solid #3ddc84; border-radius: 8px;
              color: #3ddc84; font-size: 15px; font-weight: bold; letter-spacing: 2px;
              padding: 12px; }
QPushButton:hover { background: #1a4529; }
"""
POWER_OFF = """
QPushButton { background: #2a1113; border: 1px solid #e10600; border-radius: 8px;
              color: #ff5f5a; font-size: 15px; font-weight: bold; letter-spacing: 2px;
              padding: 12px; }
QPushButton:hover { background: #371417; }
"""

# "Alles beenden" beendet auch den Server - das soll man sehen, bevor man klickt.
DANGER = """
QPushButton { background: #1b1b22; border: 1px solid #e10600; border-radius: 6px;
              color: #ff5f5a; font-weight: bold; letter-spacing: 1px; padding: 8px; }
QPushButton:hover { background: #2a1113; }
"""


def _dot(color: str) -> QLabel:
    lbl = QLabel()
    lbl.setFixedSize(10, 10)
    lbl.setStyleSheet(f"background: {color}; border-radius: 5px;")
    return lbl


class SubsystemsPanel(QWidget):
    """Schaltbrett: Server starten, HUD an/aus, HUD platzieren."""

    START_GRACE_MS = 25000   # so lange gilt der Server als "startet gerade"

    def __init__(self, state: dict, hud, api: ApiClient):
        super().__init__()
        self.state = state
        self.hud = hud
        self.api = api
        self._update_manuell = False   # True = Suche kam per Knopf, darf sich melden
        self._starting = False   # True, solange auf den frisch gestarteten Server gewartet wird
        self._loading = False    # True, waehrend die Spinboxen nachgezogen werden
        self._server_proc = None  # von hier gestarteter Server (Popen), sonst None
        self._server_name = "main.exe"  # was start_server() zuletzt gestartet hat

        self.setWindowTitle("KERS Subsystems")
        self.setWindowIcon(make_icon())
        self.setStyleSheet(STYLE)
        self.setMinimumWidth(460)
        self.resize(500, 520)

        self._start_timeout = QTimer(self)
        self._start_timeout.setSingleShot(True)
        self._start_timeout.setInterval(self.START_GRACE_MS)
        self._start_timeout.timeout.connect(self._start_gave_up)

        outer = QVBoxLayout(self)
        outer.setContentsMargins(12, 12, 12, 12)
        outer.setSpacing(8)
        outer.addLayout(self._build_header())
        outer.addWidget(self._build_power())
        outer.addWidget(self._build_hud_group())
        outer.addWidget(self._build_obs_group())
        outer.addStretch(1)
        outer.addLayout(self._build_footer())

        self.api.statusChanged.connect(self._apply_status)

        # HUD -> Oberflaeche (z.B. wenn du das Fenster mit der Maus verschiebst)
        self.hud.hudGeometryChanged.connect(self._apply_geometry)
        self.hud.hudLockedChanged.connect(
            lambda v: self._set_checked(self.cb_locked, v))
        self.hud.hudVisibilityChanged.connect(self._apply_power)
        # Beenden geht auch IN der Szene (Fertig-Knopf / Esc) - dann muss der
        # Knopf hier nachziehen. Nur der QML-Renderer kennt das.
        if hasattr(self.hud, "hudLayoutEditChanged"):
            self.hud.hudLayoutEditChanged.connect(self._apply_layout_edit)

        self.move(int(state["panel_x"]), int(state["panel_y"]))
        if bool(state.get("panel_on_top")):
            self._set_on_top(True)

        # ── Update-Suche ────────────────────────────────────────────────────
        # Parent ist bewusst self: ohne Qt-Ownership raeumt der Python-GC das
        # Objekt weg, waehrend seine Netzwerkabfrage noch laeuft (Absturz).
        self._updater = Updater(self)
        self._updater.updateAvailable.connect(self._update_gefunden)
        self._updater.upToDate.connect(self._update_aktuell)
        self._updater.failed.connect(self._update_fehler)
        self._updater.progress.connect(self._update_fortschritt)
        self._updater.ready.connect(self._update_bereit)
        self._updater.releasesGeladen.connect(self._releases_da)
        # Einmal beim Start still nachsehen - nur melden, nie von selbst laden.
        QTimer.singleShot(3000, self._updater.check)
        # Kurz danach die Liste aller Fassungen fuer die Auswahl unten. Bewusst
        # versetzt: zwei Anfragen auf einmal waeren beim Start unnoetig, und
        # GitHub zaehlt ohne Anmeldung nur 60 pro Stunde.
        QTimer.singleShot(4500, self._updater.releases_laden)

    # ── Kopf: Status + Server starten ───────────────────────────────────────
    def _build_header(self) -> QVBoxLayout:
        box = QVBoxLayout()
        box.setSpacing(6)

        # Blitz-Logo neben der Ueberschrift. Dieselbe Datei, die auch als Tray- und
        # Fenster-Icon dient (common.make_icon) - hier nur groesser.
        title = QHBoxLayout()
        title.setSpacing(8)
        bolt = QLabel()
        # ⚠ KEIN setFixedSize: der Blitz ist hochkant (303x469). In einem festen
        # Quadrat sieht er gestaucht aus, und je nach Skalierung des Bildschirms
        # kann er darin auch verzerrt werden. Ohne feste Groesse richtet sich das
        # Label exakt nach dem Bild, und pixmap() haelt das Seitenverhaeltnis.
        bolt.setPixmap(make_icon().pixmap(34, 34))
        title.addWidget(bolt)
        head = QLabel("KERS SUBSYSTEMS")
        head.setObjectName("head")
        title.addWidget(head)
        title.addStretch(1)
        box.addLayout(title)

        row = QHBoxLayout()
        row.setSpacing(6)
        self.dot_server = _dot("#e10600")
        self.lbl_server = QLabel("Server ...")
        self.dot_udp = _dot("#e10600")
        self.lbl_udp = QLabel("UDP ...")
        row.addWidget(self.dot_server)
        row.addWidget(self.lbl_server)
        row.addSpacing(10)
        row.addWidget(self.dot_udp)
        row.addWidget(self.lbl_udp)
        row.addStretch(1)
        # Version rechts in derselben Zeile: immer im Blick, kostet keine eigene.
        # Liegt ein Update bereit, faerbt sie sich und zeigt "v0.0.1 -> 0.0.2".
        self.lbl_version = QLabel(f"v{APP_VERSION}")
        self.lbl_version.setObjectName("hint")
        self.lbl_version.setToolTip("Version dieser Anwendung")
        row.addWidget(self.lbl_version)
        box.addLayout(row)

        # Erscheint nur, wenn es wirklich etwas Neues gibt - wie der
        # Renderer-Hinweis weiter unten.
        self.btn_update = QPushButton()
        self.btn_update.hide()
        self.btn_update.clicked.connect(self._update_holen)
        box.addWidget(self.btn_update)

        srv = QHBoxLayout()
        self.btn_server = QPushButton("Start Server")
        self.btn_server.setToolTip(
            f"Startet main.exe (oder main.py, falls keine EXE da ist) aus\n"
            f"{ROOT} in einem eigenen Konsolenfenster, damit du die\n"
            "Ausgaben mitlesen kannst.")
        self.btn_server.clicked.connect(self.start_server)
        self.btn_stop = QPushButton("Stop Server")
        self.btn_stop.setToolTip(
            "Beendet den Server. Einen von hier gestarteten sofort, einen fremden\n"
            "(run.bat, eigenes Terminal) erst nach Rueckfrage - der wird ueber den\n"
            "Port gesucht und koennte theoretisch etwas anderes sein.")
        self.btn_stop.setEnabled(False)
        self.btn_stop.clicked.connect(lambda: self.stop_server())
        srv.addWidget(self.btn_server)
        srv.addWidget(self.btn_stop)
        box.addLayout(srv)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet("background: rgba(255,255,255,0.09); max-height: 1px;")
        box.addWidget(line)
        return box

    def start_server(self) -> None:
        """Server in einem eigenen Konsolenfenster starten.

        Bevorzugt main.exe (gebaute EXE, braucht kein Python/venv) - faellt
        sonst auf main.py + venv zurueck (Dev-Betrieb ohne Build).

        Bewusst mit sichtbarer Konsole (CREATE_NEW_CONSOLE) statt still im
        Hintergrund: main.py/main.exe schreibt dort seine Meldungen hin, und
        genau die will man sehen, wenn z.B. UDP 20777 schon belegt ist.
        """
        exe, payload_err = _ensure_server_payload()
        if payload_err:
            # Kein stiller Fehlschlag: sonst laeuft entweder gar nichts oder - schlimmer -
            # der alte Server weiter, und man sucht den Fehler im Overlay.
            QMessageBox.warning(self, "Start Server", payload_err)
            self.btn_server.setEnabled(True)
            self.btn_server.setText("Start Server")
            return
        script = ROOT / "main.py"

        if exe is not None and exe.is_file():
            cmd = [str(exe)]
            self._server_name = "main.exe"
        elif script.is_file():
            python = ROOT / "venv" / "Scripts" / "python.exe"
            if not python.is_file():
                python = Path(sys.executable)
            cmd = [str(python), str(script)]
            self._server_name = "main.py"
        else:
            self.btn_server.setText(f"main.exe/main.py nicht gefunden ({ROOT})")
            self.btn_server.setEnabled(False)
            return

        try:
            # ⚠ env: ohne die Saeuberung erbt main.exe unsere _PYI_-Variablen und
            # packt sich GAR NICHT aus - sie laeuft dann aus dem _MEI-Ordner des
            # HUD. Das faellt nur nicht auf, solange das HUD lebt; beendet man es
            # zuerst, wird der Ordner unter der laufenden main.exe weggeraeumt.
            # Ausfuehrlich bei common.umwelt_ohne_pyinstaller.
            self._server_proc = subprocess.Popen(
                cmd, cwd=str(ROOT),
                creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0),
                env=umwelt_ohne_pyinstaller(),
            )
        except Exception as e:  # pylint: disable=broad-exception-caught
            self.btn_server.setText(f"Start fehlgeschlagen: {e}")
            return

        self._starting = True
        self.btn_server.setEnabled(False)
        self.btn_server.setText("Server Starting ...")
        self._start_timeout.start()

    def _start_gave_up(self) -> None:
        """Nach der Wartezeit ohne Lebenszeichen wieder freigeben."""
        self._starting = False
        self.btn_server.setEnabled(True)
        self.btn_server.setText("Start Server (letzter Versuch lief nicht an)")

    # ── Update ──────────────────────────────────────────────────────────────
    def _update_suchen(self) -> None:
        """Suche von Hand - anders als beim Start darf sie sich hier melden."""
        self._update_manuell = True
        # Wer von Hand sucht, will es wissen - die Stummschaltung aus einem
        # frueheren Fassungswechsel ist damit aufgehoben.
        self.state["update_stumm"] = False
        self.lbl_version.setToolTip("Suche laeuft …")
        self._updater.check()
        self._updater.releases_laden()

    def _update_gefunden(self, version: str, groesse: int, datum: str) -> None:
        mb = groesse / (1024 * 1024)
        # Nach einem bewussten Wechsel auf eine bestimmte Fassung nicht bei jedem
        # Start dieselbe neuere anbieten. Im Tooltip steht sie trotzdem, und die
        # Suche von Hand zeigt sie wieder - die ist ja ausdruecklich gewollt.
        if self.state.get("update_stumm") and not self._update_manuell:
            self.lbl_version.setToolTip(
                f"Neuere Fassung {version} vom {datum} liegt bereit ({mb:.0f} MB).\n"
                "Ausgeblendet, weil du diese Fassung bewusst gewaehlt hast - "
                "'Nach Update suchen' zeigt sie wieder.")
            return
        self.lbl_version.setText(f"v{APP_VERSION} → {version}")
        self.lbl_version.setStyleSheet("color:#e10600;font-weight:bold")
        self.lbl_version.setToolTip(
            f"Neue Fassung {version} vom {datum}\n{mb:.0f} MB")
        self.btn_update.setText(f"Auf {version} aktualisieren  ({mb:.0f} MB)")
        self.btn_update.setEnabled(True)
        self.btn_update.show()

    def _update_aktuell(self) -> None:
        self.lbl_version.setToolTip("Version dieser Anwendung — auf dem neuesten Stand")
        if self._update_manuell:
            self._update_manuell = False
            QMessageBox.information(self, "Nach Update suchen",
                                    f"Version {APP_VERSION} ist die neueste.")

    def _update_fehler(self, text: str) -> None:
        # Beim stillen Start-Check nicht mit einem Dialog dazwischenfahren; die
        # Meldung steht im Tooltip und wird beim Klick auf "Suchen" gezeigt.
        self.lbl_version.setToolTip(f"Update-Suche: {text}")
        # Ein abgebrochener Download darf die Auswahl unten nicht dauerhaft
        # sperren - sonst kommt man ohne Neustart nicht mehr an eine Fassung.
        if hasattr(self, "btn_fassung"):
            self.btn_fassung.setEnabled(bool(self._updater.releases))
            self.btn_update.hide()
        if self._update_manuell:
            self._update_manuell = False
            QMessageBox.information(self, "Nach Update suchen", text)

    def _update_holen(self) -> None:
        self.btn_update.setEnabled(False)
        self.btn_update.setText("Wird geladen …")
        self._updater.download()

    def _update_fortschritt(self, geladen: int, gesamt: int) -> None:
        if gesamt > 0:
            self.btn_update.setText(
                f"Wird geladen … {geladen * 100 // gesamt} %"
                f"  ({geladen / (1024 * 1024):.0f} von {gesamt / (1024 * 1024):.0f} MB)")

    def _update_bereit(self, pfad: str) -> None:
        self.btn_update.setText("Neu starten und übernehmen")
        self.btn_update.setEnabled(True)
        try:
            self.btn_update.clicked.disconnect()
        except (RuntimeError, TypeError):
            pass
        self.btn_update.clicked.connect(self._update_anwenden)

    # ── Vorherige Versionen ─────────────────────────────────────────────────
    def _such_completer(self) -> None:
        """Den EINGEBAUTEN Completer der ComboBox scharfstellen.

        ⚠ NICHT setCompleter() mit einem frisch erzeugten QCompleter aufrufen:
        der haette kein Modell und faende deshalb nie etwas - genau daran ist
        die Suche im ersten Anlauf gescheitert. Man konnte nur die exakte
        Nummer eintippen und "Wechseln" druecken. Nur der eingebaute Completer
        haengt am Listenmodell der ComboBox und zieht beim Fuellen von selbst
        mit.

        MatchContains statt der Vorgabe MatchStartsWith, damit auch "232" oder
        "08-13" etwas findet und nicht nur der Anfang der Zeile zaehlt.
        """
        c = self.cmb_fassung.completer()
        if c is None:
            return
        c.setCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        c.setFilterMode(Qt.MatchFlag.MatchContains)
        c.setCompletionMode(QCompleter.CompletionMode.PopupCompletion)

    def eventFilter(self, obj, event):
        """Klick ins Textfeld zeigt die Liste."""
        if (getattr(self, "cmb_fassung", None) is not None
                and obj is self.cmb_fassung.lineEdit()
                and event.type() == QEvent.Type.MouseButtonPress
                and self.cmb_fassung.count() > 0):
            # Verzoegert: waehrend der Klick noch verarbeitet wird, wuerde das
            # Textfeld die gerade geoeffnete Liste sofort wieder schliessen.
            QTimer.singleShot(0, self._liste_zeigen)
        return super().eventFilter(obj, event)

    def _liste_zeigen(self) -> None:
        c = self.cmb_fassung.completer()
        if c is None:
            return
        # Leerer Vorsatz = alles passt: der erste Klick zeigt die volle Liste,
        # ab dem ersten Zeichen bleibt nur noch, was dazu passt.
        c.setCompletionPrefix(self.cmb_fassung.currentText())
        c.complete()

    def _releases_da(self, liste: list) -> None:
        """Die Liste von GitHub ist da - Auswahlfeld fuellen."""
        self.cmb_fassung.clear()
        if not liste:
            self.cmb_fassung.lineEdit().setPlaceholderText("keine Fassungen gefunden")
            return
        for r in liste:
            hier = "  (laeuft)" if r["version"] == APP_VERSION else ""
            self.cmb_fassung.addItem(
                f"{r['version']}   {r['published']}   "
                f"{r['size'] / (1024 * 1024):.0f} MB{hier}", r["version"])
        self.cmb_fassung.setCurrentIndex(-1)
        self.cmb_fassung.lineEdit().setPlaceholderText("Vorherige Versionen")
        # Beim Fuellen wechselt das Modell der ComboBox - der Completer haengt
        # danach unter Umstaenden am alten. Deshalb hier noch einmal setzen.
        self._such_completer()
        self.btn_fassung.setEnabled(True)

    def _gewaehlte_fassung(self) -> str:
        """Version aus dem Feld holen - egal ob ausgewaehlt oder getippt.

        Bei Auswahl steht im Feld die ganze Zeile ("0.1.1   2026-08-13   232 MB"),
        die nackte Nummer liegt als Nutzdatum daneben. Getippt wird dagegen nur
        die Nummer. Deshalb erst das Nutzdatum versuchen, dann den Text.
        """
        i = self.cmb_fassung.currentIndex()
        if i >= 0 and self.cmb_fassung.itemText(i) == self.cmb_fassung.currentText():
            return str(self.cmb_fassung.itemData(i) or "")
        return self.cmb_fassung.currentText().strip().split()[0] \
            if self.cmb_fassung.currentText().strip() else ""

    def _fassung_wechseln(self) -> None:
        version = self._gewaehlte_fassung()
        if not version:
            QMessageBox.information(self, "Andere Fassung",
                                    "Bitte eine Version waehlen oder eintippen.")
            return
        fehler = self._updater.waehlen(version)
        if fehler:
            QMessageBox.information(self, "Andere Fassung", fehler)
            return

        if als_zahlen(version) < ERSTE_MIT_DATENORDNER:
            antwort = QMessageBox.question(
                self, "Aeltere Fassung",
                f"Version {version} ist aelter als 0.1.1 und kennt den Ordner "
                f"'{DATA_DIR.name}' noch nicht.\n\n"
                "Sie sucht ihre Dateien neben der EXE, findet dort nichts und legt "
                "frische Vorgaben an - WM-Stand und Einstellungen wirken dann "
                "verschwunden.\n\n"
                "Verloren geht nichts: beim Zurueckwechseln auf eine neuere Fassung "
                "sind die echten Daten wieder da.\n\nTrotzdem laden?")
            if antwort != QMessageBox.StandardButton.Yes:
                return

        # Bewusste Wahl: die stille Suche beim Start soll jetzt nicht bei jedem
        # Mal die neueste Fassung anbieten. "Nach Update suchen" hebt das auf.
        self.state["update_stumm"] = True
        self.btn_fassung.setEnabled(False)
        self.btn_update.setText(f"Version {version} wird geladen …")
        self.btn_update.setEnabled(False)
        self.btn_update.show()
        self._updater.download()

    def _update_anwenden(self) -> None:
        fehler = self._updater.tausch_starten()
        if fehler:
            QMessageBox.warning(self, "Update", fehler)
            return
        # Der Helfer wartet, bis dieser Prozess weg ist - also jetzt beenden.
        QApplication.quit()

    # ── Server beenden ──────────────────────────────────────────────────────
    def _server_port(self) -> int:
        try:
            return urlparse(self.api.base_url).port or 5100
        except ValueError:
            return 5100

    def server_target(self):
        """Wen wuerden wir beenden? -> ("own"|"foreign"|"none", pid, name)"""
        proc = self._server_proc
        if proc is not None and proc.poll() is None:
            return "own", proc.pid, self._server_name
        pid = _find_listener_pid(self._server_port())
        if pid is None:
            return "none", None, ""
        return "foreign", pid, _process_name(pid)

    def stop_server(self, confirm: bool = True) -> bool:
        """Server beenden. Eigenen Kindprozess direkt, fremden nur nach Rueckfrage."""
        kind, pid, name = self.server_target()

        if kind == "none":
            self.btn_stop.setEnabled(False)
            self.btn_stop.setText(f"nichts auf Port {self._server_port()}")
            QTimer.singleShot(2500, lambda: self.btn_stop.setText("Stop Server"))
            return False

        if kind == "foreign":
            # Wir haben ihn nur am Port erkannt - also lieber zweimal hinsehen.
            lname = name.lower()
            if "python" not in lname and "main" not in lname:
                QMessageBox.warning(
                    self, "Stop Server",
                    f"Auf Port {self._server_port()} laeuft "
                    f"{name or 'ein unbekannter Prozess'} (PID {pid}).\n\n"
                    "Das sieht nicht nach main.py/main.exe aus - es wird nichts beendet.")
                return False
            if confirm:
                answer = QMessageBox.question(
                    self, "Stop Server",
                    f"Der Server wurde nicht von hier gestartet.\n\n"
                    f"{name} (PID {pid}) auf Port {self._server_port()} jetzt beenden?")
                if answer != QMessageBox.StandardButton.Yes:
                    return False

        _kill_tree(pid)
        self._server_proc = None
        self._starting = False
        self._start_timeout.stop()
        self.btn_stop.setEnabled(False)
        QTimer.singleShot(400, self.api.refresh_status)
        return True

    # ── Alles auf einmal ────────────────────────────────────────────────────
    def shutdown_all(self) -> None:
        """Server beenden, HUD schliessen, Subsystems beenden - nach einer Rueckfrage."""
        kind, pid, name = self.server_target()
        if kind == "own":
            what = "Der Server und das HUD werden beendet."
        elif kind == "foreign":
            what = f"Der Server ({name}, PID {pid}) und das HUD werden beendet."
        else:
            what = "Es laeuft kein Server - nur das HUD wird beendet."

        answer = QMessageBox.question(
            self, "Alles beenden", f"{what}\n\nWeitermachen?")
        if answer != QMessageBox.StandardButton.Yes:
            return

        if kind != "none":
            self.stop_server(confirm=False)   # Rueckfrage gab es schon
        QApplication.instance().quit()

    # ── Hauptschalter + An Bildschirm anpassen ──────────────────────────────
    def _build_power(self) -> QWidget:
        wrap = QWidget()
        lay = QVBoxLayout(wrap)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)

        self.btn_power = QPushButton()
        self.btn_power.setCheckable(True)
        self.btn_power.setChecked(bool(self.state["visible"]))
        self.btn_power.clicked.connect(self.hud.set_hud_visible)
        lay.addWidget(self.btn_power)
        self._apply_power(bool(self.state["visible"]))

        row = QHBoxLayout()
        btn_fill = QPushButton("An Bildschirm anpassen")
        btn_fill.setToolTip(
            "Setzt das HUD auf die volle Groesse des Bildschirms, auf dem es gerade liegt,\n"
            "und sperrt es dabei (klick-durch) - sonst laege eine bildschirmfuellende\n"
            "Bearbeiten-Flaeche ueber allem und man kaeme an nichts mehr heran.")
        btn_fill.clicked.connect(self.fill_screen)
        btn_center = QPushButton("Zentrieren")
        btn_center.clicked.connect(self._center_hud)
        btn_reload = QPushButton("Neu laden")
        btn_reload.clicked.connect(self.hud.reload_page)
        row.addWidget(btn_fill, 2)
        row.addWidget(btn_center, 1)
        row.addWidget(btn_reload, 1)
        lay.addLayout(row)

        # Layout bearbeiten - nur im QML-Renderer, der Web-Renderer kennt es nicht.
        if hasattr(self.hud, "set_layout_edit"):
            self.btn_layout = QPushButton("Layout bearbeiten")
            self.btn_layout.setCheckable(True)
            self.btn_layout.setToolTip(
                "Bausteine mit der Maus an ihren Platz ziehen.\n\n"
                "Entsperrt das HUD dabei automatisch - gesperrt gehen alle Klicks\n"
                "durch das Fenster hindurch und die Szene bekaeme gar keine Maus.\n"
                "Beendet wird IN der Szene (Fertig-Knopf oben oder Esc): das\n"
                "entsperrte HUD liegt bildschirmgross ueber diesem Fenster.\n"
                "Danach wird der vorherige Sperrzustand wiederhergestellt.")
            self.btn_layout.toggled.connect(self._layout_edit_umschalten)
            lay.addWidget(self.btn_layout)
        return wrap

    def _layout_edit_umschalten(self, on: bool) -> None:
        self.hud.set_layout_edit(bool(on))
        self._beschrifte_layout_knopf(bool(on))

    def _apply_layout_edit(self, on: bool) -> None:
        """HUD -> Oberflaeche: in der Szene auf Fertig geklickt (oder Esc)."""
        self._set_checked(self.btn_layout, bool(on))
        self._beschrifte_layout_knopf(bool(on))

    def _beschrifte_layout_knopf(self, on: bool) -> None:
        self.btn_layout.setText("Layout bearbeiten - FERTIG" if on
                                else "Layout bearbeiten")
        self.btn_layout.setStyleSheet(DANGER if on else "")

    def _apply_power(self, on: bool) -> None:
        self.btn_power.blockSignals(True)
        self.btn_power.setChecked(bool(on))
        self.btn_power.blockSignals(False)
        self.btn_power.setText("HUD IST AN" if on else "HUD IST AUS")
        self.btn_power.setStyleSheet(POWER_ON if on else POWER_OFF)

    def fill_screen(self) -> None:
        """Auf den Bildschirm legen, auf dem das HUD gerade steht."""
        screen = self.hud.screen() or self.screen()
        geo = screen.geometry()          # komplette Flaeche, inkl. Taskleistenbereich
        self.hud.apply_geometry(geo.x(), geo.y(), geo.width(), geo.height())
        # Zwingend: unverriegelt laege jetzt eine bildschirmgrosse Klickflaeche
        # ueber allem - man kaeme weder ans Spiel noch an dieses Fenster.
        self.hud.set_locked(True)
        if not self.state["visible"]:
            self.hud.set_hud_visible(True)

    def _center_hud(self) -> None:
        screen = self.hud.screen() or self.screen()
        area = screen.availableGeometry()
        w, h = self.hud.width(), self.hud.height()
        self.hud.apply_geometry(area.x() + (area.width() - w) // 2,
                                area.y() + (area.height() - h) // 2, w, h)

    # ── HUD-Fenster: platzieren ─────────────────────────────────────────────
    def _build_hud_group(self) -> QGroupBox:
        group = QGroupBox("HUD-FENSTER")
        lay = QVBoxLayout(group)
        lay.setSpacing(6)

        self.cb_locked = QCheckBox("Gesperrt - Klicks gehen durch zum Spiel")
        self.cb_locked.setChecked(bool(self.state["locked"]))
        self.cb_locked.toggled.connect(self.hud.set_locked)
        lay.addWidget(self.cb_locked)

        hint = QLabel("Entsperrt = roter Rahmen, Fenster laesst sich ziehen und skalieren.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        lay.addWidget(hint)

        # Renderer. Der Wechsel braucht ein anderes Fenster (QQuickView vs.
        # QWebEngineView), laesst sich also nicht im laufenden Betrieb umhaengen -
        # deshalb nur merken und beim naechsten Start anwenden.
        row = QHBoxLayout()
        row.addWidget(QLabel("Renderer"))
        self.cmb_renderer = QComboBox()
        self.cmb_renderer.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.cmb_renderer.setMinimumContentsLength(14)
        self.cmb_renderer.setToolTip(
            "QML ist die neue Darstellung - kein Chromium, echter Alphakanal.\n"
            "Web zeigt weiter die HTML-Seiten aus templates/.\n\n"
            "Wirkt erst beim naechsten Start des HUDs.")
        for key, label in RENDERERS:
            self.cmb_renderer.addItem(label, key)
        idx = self.cmb_renderer.findData(self.state.get("renderer", "qml"))
        self.cmb_renderer.setCurrentIndex(idx if idx >= 0 else 0)
        self.cmb_renderer.currentIndexChanged.connect(self._on_renderer)
        row.addWidget(self.cmb_renderer, 1)
        lay.addLayout(row)

        self.lbl_renderer_hint = QLabel("")
        self.lbl_renderer_hint.setObjectName("hint")
        self.lbl_renderer_hint.setWordWrap(True)
        self.lbl_renderer_hint.hide()
        lay.addWidget(self.lbl_renderer_hint)

        row = QHBoxLayout()
        row.addWidget(QLabel("Seite"))
        self.cmb_page = QComboBox()
        # Sonst zieht sich die Box auf den laengsten Eintrag auf und sprengt das Fenster.
        self.cmb_page.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.cmb_page.setMinimumContentsLength(14)
        for label, path in PAGES:
            self.cmb_page.addItem(label, path)
        idx = self.cmb_page.findData(self.state["page"])
        self.cmb_page.setCurrentIndex(idx if idx >= 0 else 0)
        self.cmb_page.currentIndexChanged.connect(
            lambda: self.hud.set_page(self.cmb_page.currentData()))
        row.addWidget(self.cmb_page, 1)
        lay.addLayout(row)

        # Renderrate. Wirkt sofort (per JavaScript in die Seite), ohne Neuladen.
        row = QHBoxLayout()
        row.addWidget(QLabel("Overlay-Hz"))
        self.cmb_hz = QComboBox()
        self.cmb_hz.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.cmb_hz.setMinimumContentsLength(14)
        self.cmb_hz.setToolTip(
            "Wie oft das Overlay hoechstens neu zeichnet.\n\n"
            "Wirkt auf drei Dinge: wie oft der Server pusht, wie oft die Seite\n"
            "neu rendert, und wie schnell die Pit-/Hotlap-Zeiten hochzaehlen\n"
            "(die laufen sonst mit der vollen Bildwiederholrate des Monitors).\n\n"
            "Gilt nur fuers HUD - OBS und Handy bleiben bei voller Rate.\n"
            "Nur die Sparfassung /opti wertet das aus.")
        for value, label in HZ_CHOICES:
            self.cmb_hz.addItem(label, value)
        idx = self.cmb_hz.findData(int(self.state.get("hz", 30)))
        self.cmb_hz.setCurrentIndex(idx if idx >= 0 else 0)
        self.cmb_hz.currentIndexChanged.connect(
            lambda: self.hud.set_hz(self.cmb_hz.currentData()))
        row.addWidget(self.cmb_hz, 1)
        lay.addLayout(row)

        grid = QGridLayout()
        grid.setHorizontalSpacing(6)
        self.sp_x = self._spin(-10000, 10000, self.state["x"])
        self.sp_y = self._spin(-10000, 10000, self.state["y"])
        self.sp_w = self._spin(240, 10000, self.state["w"])
        self.sp_h = self._spin(160, 10000, self.state["h"])
        for col, (cap, spin) in enumerate([("X", self.sp_x), ("Y", self.sp_y),
                                           ("Breite", self.sp_w), ("Hoehe", self.sp_h)]):
            grid.addWidget(QLabel(cap), 0, col)
            grid.addWidget(spin, 1, col)
            spin.valueChanged.connect(self._on_geometry_spin)
        lay.addLayout(grid)

        self.cb_on_top = QCheckBox("Dieses Fenster immer im Vordergrund")
        self.cb_on_top.setChecked(bool(self.state.get("panel_on_top")))
        self.cb_on_top.toggled.connect(self._set_on_top)
        lay.addWidget(self.cb_on_top)

        self._apply_renderer_ui()
        return group

    def _on_renderer(self) -> None:
        choice = self.cmb_renderer.currentData()
        self.state["renderer"] = choice
        self.lbl_renderer_hint.setText(
            "Wirkt beim naechsten Start des HUDs (Tray -> Nur HUD beenden, "
            "dann start_hud.bat).")
        self.lbl_renderer_hint.show()
        self._apply_renderer_ui()

    def _apply_renderer_ui(self) -> None:
        """Was zum jeweils ANDEREN Renderer gehoert, ausgrauen statt verstecken.

        Verstecken waere unehrlich: die Einstellung existiert ja weiter und gilt,
        sobald wieder umgeschaltet wird.
        """
        qml = self.state.get("renderer", "qml") == "qml"
        # "Seite" gibt es nur im Web-Renderer - in QML sind alle Bausteine EINE
        # Szene, und was davon zu sehen ist, steht in /settings.
        self.cmb_page.setEnabled(not qml)
        self.cmb_page.setToolTip(
            "Nur fuer den Web-Renderer. In QML sind alle Bausteine eine einzige\n"
            "Szene - sichtbar/unsichtbar wird in /settings geschaltet."
            if qml else "Welche Seite im HUD-Fenster haengt.")
        if hasattr(self, "cb_chroma"):
            # Alle OBS-Schalter haengen an der QML-Szene; im Web-Renderer bleibt es
            # beim Alphakanal von Chromium und den Browser-Quellen.
            self.cb_obs_window.setEnabled(qml)
            self.sp_obs_w.setEnabled(qml)
            self.sp_obs_h.setEnabled(qml)
            self.cb_chroma.setEnabled(qml)
            self.cb_findable.setEnabled(qml)
            self.cmb_chroma_color.setEnabled(
                qml and (self.cb_chroma.isChecked() or self.cb_obs_window.isChecked()))

    # ── OBS ─────────────────────────────────────────────────────────────────
    def _build_obs_group(self) -> QGroupBox:
        group = QGroupBox("OBS")
        lay = QVBoxLayout(group)
        lay.setSpacing(6)

        hint = QLabel(
            "Empfohlen: das OBS-Fenster einschalten und es in OBS per Fensteraufnahme "
            "holen („KERS OBS“), dazu einen Farbschluessel-Filter auf Magenta. "
            "Das HUD-Fenster selbst laesst sich zwar auch aufnehmen, friert in der "
            "Fensteraufnahme aber auf einem Standbild ein – warum, steht in der "
            "README.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        lay.addWidget(hint)

        self.cb_obs_window = QCheckBox("Eigenes OBS-Fenster")
        self.cb_obs_window.setChecked(bool(self.state.get("obs_window")))
        self.cb_obs_window.setToolTip(
            "Oeffnet ein zweites, UNDURCHSICHTIGES Fenster in Canvas-Groesse mit\n"
            "derselben Szene. Nur dieses laesst sich zuverlaessig per Fensteraufnahme\n"
            "holen: das HUD-Fenster ist durchsichtig, und ein durchsichtiges Fenster\n"
            "liefert der Windows Graphics Capture keine neuen Bilder mehr.\n\n"
            "Das HUD auf dem Desktop kann dabei ruhig aus bleiben.")
        self.cb_obs_window.toggled.connect(self.set_obs_window)
        lay.addWidget(self.cb_obs_window)

        row = QHBoxLayout()
        row.addWidget(QLabel("Canvas"))
        self.sp_obs_w = self._spin(320, 10000, int(self.state.get("obs_w") or 1920))
        self.sp_obs_h = self._spin(180, 10000, int(self.state.get("obs_h") or 1080))
        for spin in (self.sp_obs_w, self.sp_obs_h):
            spin.setToolTip("Groesse des OBS-Fensters - am besten die OBS-Canvas.")
            spin.valueChanged.connect(self._on_obs_canvas)
            row.addWidget(spin)
        row.addStretch(1)
        lay.addLayout(row)

        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        line.setStyleSheet("background: rgba(255,255,255,0.09); max-height: 1px;")
        lay.addWidget(line)

        hint2 = QLabel("Nur noetig, wenn du statt dessen direkt das HUD-Fenster "
                       "aufnehmen willst:")
        hint2.setObjectName("hint")
        hint2.setWordWrap(True)
        lay.addWidget(hint2)

        self.cb_findable = QCheckBox("HUD-Fenster fuer OBS auffindbar machen")
        self.cb_findable.setChecked(bool(self.state.get("obs_findable")))
        self.cb_findable.setToolTip(
            "Das HUD laeuft normalerweise als Werkzeugfenster (Qt.Tool). Windows\n"
            "setzt dafuer WS_EX_TOOLWINDOW - und genau diesen Stil wirft OBS aus\n"
            "seiner Fensterliste. Deshalb ist das HUD dort sonst nicht zu finden.\n\n"
            "Der Schalter nimmt den Stil weg. Preis: das Fenster bekommt einen\n"
            "Eintrag in der Taskleiste und taucht im Alt-Tab auf. Beides haengt an\n"
            "derselben Eigenschaft, das eine gibt es nicht ohne das andere.")
        self.cb_findable.toggled.connect(self._set_findable)
        lay.addWidget(self.cb_findable)

        self.cb_chroma = QCheckBox("HUD-Fenster einfarbig statt durchsichtig")
        self.cb_chroma.setChecked(bool(self.state.get("obs_chroma")))
        self.cb_chroma.setToolTip(
            "Faerbt den Fensterhintergrund einfarbig und macht ALLE Panels deckend.\n"
            "Ohne das Zweite wuerde die Schluesselfarbe durch die halbtransparenten\n"
            "Panels scheinen und der Farbschluessel in OBS sie anteilig mitfressen.")
        self.cb_chroma.toggled.connect(self._set_chroma)
        lay.addWidget(self.cb_chroma)

        row = QHBoxLayout()
        row.addWidget(QLabel("Schluesselfarbe"))
        self.cmb_chroma_color = QComboBox()
        self.cmb_chroma_color.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.cmb_chroma_color.setMinimumContentsLength(12)
        # Magenta zuerst: Gruen kollidiert mit den Sektorfarben und dem DRS-Gruen,
        # Blau mit Alpine/Williams/RB. Magenta kommt im Overlay nirgends vor.
        for label, value in (("Magenta (empfohlen)", "#FF00FF"),
                             ("Gruen", "#00FF00"),
                             ("Blau", "#0000FF")):
            self.cmb_chroma_color.addItem(label, value)
        idx = self.cmb_chroma_color.findData(
            str(self.state.get("obs_chroma_color") or "#FF00FF").upper())
        self.cmb_chroma_color.setCurrentIndex(idx if idx >= 0 else 0)
        self.cmb_chroma_color.currentIndexChanged.connect(self._set_chroma)
        row.addWidget(self.cmb_chroma_color, 1)
        lay.addLayout(row)

        self._apply_renderer_ui()
        return group

    def _set_chroma(self) -> None:
        qml = self.state.get("renderer", "qml") == "qml"
        on = self.cb_chroma.isChecked()
        color = self.cmb_chroma_color.currentData()
        # Die Schluesselfarbe gilt fuer beide Wege - fuer das einfarbige HUD-Fenster
        # UND fuer das OBS-Fenster, das immer einfarbig ist.
        self.cmb_chroma_color.setEnabled(
            qml and (on or self.cb_obs_window.isChecked()))
        setter = getattr(self.hud, "set_chroma", None)
        if setter is not None:
            setter(on, color)
        obs = getattr(self.hud, "obs_window", None)
        if obs is not None:
            obs.set_chroma_color(color)

    def _set_findable(self, on: bool) -> None:
        setter = getattr(self.hud, "set_findable", None)
        if setter is not None:
            setter(on)

    # ── Eigenes OBS-Fenster ─────────────────────────────────────────────────
    def set_obs_window(self, on: bool) -> None:
        """Das Aufnahmefenster oeffnen oder schliessen.

        Es wird erst beim Einschalten gebaut - wer es nicht braucht, zahlt auch
        nicht dafuer (es rendert die Szene ein zweites Mal).
        """
        self.state["obs_window"] = bool(on)
        self._set_checked(self.cb_obs_window, on)
        if self.state.get("renderer", "qml") != "qml":
            return
        obs = getattr(self.hud, "obs_window", None)
        if on:
            if obs is None:
                from obs_window import ObsWindow
                obs = ObsWindow(self.state, self.hud.bridge)
                self.hud.obs_window = obs
            obs.set_canvas(self.sp_obs_w.value(), self.sp_obs_h.value())
            obs.set_chroma_color(self.cmb_chroma_color.currentData())
            obs.show()
        elif obs is not None:
            obs.hide()

    def _on_obs_canvas(self) -> None:
        if self._loading:
            return
        self.state["obs_w"] = self.sp_obs_w.value()
        self.state["obs_h"] = self.sp_obs_h.value()
        obs = getattr(self.hud, "obs_window", None)
        if obs is not None:
            obs.set_canvas(self.sp_obs_w.value(), self.sp_obs_h.value())

    def _spin(self, lo: int, hi: int, value: int) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(lo, hi)
        spin.setValue(int(value))
        # Vier Spinboxen nebeneinander muessen in die Panelbreite passen.
        spin.setMinimumWidth(64)
        spin.setMaximumWidth(110)
        return spin

    # ── Fuss: hier geht es zu den echten Seiten ─────────────────────────────
    def _build_footer(self) -> QVBoxLayout:
        box = QVBoxLayout()
        box.setSpacing(6)

        row = QHBoxLayout()
        btn_regie = QPushButton("Regie")
        btn_regie.setToolTip("Manuelle Einblendungen: Charts, Strategie, WM-Stand")
        btn_regie.clicked.connect(lambda: webbrowser.open(f"{self.api.base_url}/regie"))
        btn_settings = QPushButton("Settings")
        btn_settings.setToolTip("Bausteine an/aus, Regler, Branding, Presets, Trackmap")
        btn_settings.clicked.connect(lambda: webbrowser.open(f"{self.api.base_url}/settings"))
        btn_upd = QPushButton("Nach Update suchen")
        btn_upd.setToolTip(
            "Fragt bei GitHub nach der neuesten Fassung.\n"
            "Beim Programmstart passiert das ohnehin einmal von selbst.")
        btn_upd.clicked.connect(self._update_suchen)
        row.addWidget(btn_regie)
        row.addWidget(btn_settings)
        row.addWidget(btn_upd)
        box.addLayout(row)

        # ── Vorherige Versionen ─────────────────────────────────────────────
        # Auswaehlen ODER tippen. Der Wechsel laeuft danach durch dieselbe
        # geprueften Kette wie ein Update (Groesse, SHA256, Austausch mit .bak)
        # - nur die Quelle ist eine andere.
        ver = QHBoxLayout()
        ver.setSpacing(6)
        lbl_ver = QLabel("Version:")
        self.cmb_fassung = QComboBox()
        self.cmb_fassung.setEditable(True)
        self.cmb_fassung.setInsertPolicy(QComboBox.InsertPolicy.NoInsert)
        self.cmb_fassung.lineEdit().setPlaceholderText("wird geladen …")
        self.cmb_fassung.setToolTip(
            "Vorherige Versionen laden.\n"
            "Ins Feld klicken zeigt die Liste, Tippen sucht darin (z.B. 0.1.1).")
        self._such_completer()
        # Klick ins Feld soll die Liste zeigen - man muss nicht wissen, dass es
        # ueberhaupt eine gibt. Der Ereignisfilter haengt am QLineEdit, nicht an
        # der ComboBox: der Klick landet im Textfeld, die ComboBox sieht ihn nie.
        self.cmb_fassung.lineEdit().installEventFilter(self)
        self.btn_fassung = QPushButton("Wechseln")
        self.btn_fassung.setEnabled(False)
        self.btn_fassung.clicked.connect(self._fassung_wechseln)
        self.cmb_fassung.lineEdit().returnPressed.connect(self._fassung_wechseln)
        ver.addWidget(lbl_ver)
        ver.addWidget(self.cmb_fassung, 1)
        ver.addWidget(self.btn_fassung)
        box.addLayout(ver)

        self.btn_quit_all = QPushButton("ALLES BEENDEN")
        self.btn_quit_all.setStyleSheet(DANGER)
        self.btn_quit_all.setToolTip("Server beenden, HUD schliessen und dieses Fenster beenden.")
        self.btn_quit_all.clicked.connect(self.shutdown_all)
        box.addWidget(self.btn_quit_all)

        hint = QLabel("Bausteine und alle Einstellungen liegen in Regie und Settings. "
                      "Fenster schliessen blendet nur aus - zum Beenden 'Alles beenden' "
                      "oder das Tray-Icon.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        box.addWidget(hint)
        return box

    # ── HUD-Reaktionen ──────────────────────────────────────────────────────
    def _on_geometry_spin(self) -> None:
        if self._loading:
            return
        self.hud.apply_geometry(self.sp_x.value(), self.sp_y.value(),
                                self.sp_w.value(), self.sp_h.value())

    def _apply_geometry(self, x: int, y: int, w: int, h: int) -> None:
        """HUD wurde bewegt -> Zahlen nachziehen (ohne Rueckkopplung)."""
        self._loading = True
        for spin, value in ((self.sp_x, x), (self.sp_y, y),
                            (self.sp_w, w), (self.sp_h, h)):
            spin.blockSignals(True)
            spin.setValue(int(value))
            spin.blockSignals(False)
        self._loading = False

    def _set_on_top(self, on: bool) -> None:
        self.state["panel_on_top"] = bool(on)
        was_visible = self.isVisible()
        flags = self.windowFlags()
        if on:
            flags |= Qt.WindowType.WindowStaysOnTopHint
        else:
            flags &= ~Qt.WindowType.WindowStaysOnTopHint
        self.setWindowFlags(flags)
        if was_visible:
            self.show()

    # ── Status vom Server ───────────────────────────────────────────────────
    def _apply_status(self, server_ok: bool, udp_ok: bool, drivers: int) -> None:
        self.dot_server.setStyleSheet(
            f"background: {'#3ddc84' if server_ok else '#e10600'}; border-radius: 5px;")
        self.lbl_server.setText("Server online" if server_ok else "Server offline")
        self.dot_udp.setStyleSheet(
            f"background: {'#3ddc84' if udp_ok else '#e10600'}; border-radius: 5px;")
        # Ohne Daten reicht der blosse Name - den Zustand sagt der Punkt davor.
        self.lbl_udp.setText(f"UDP aktiv - {drivers} Fahrer" if udp_ok else "UDP-Daten")

        if server_ok:
            self._starting = False
            self._start_timeout.stop()
            self.btn_server.setEnabled(False)
            self.btn_server.setText("Server Run")
            self.btn_stop.setEnabled(True)
            self.btn_stop.setText("Stop Server")
        else:
            self.btn_stop.setEnabled(False)
            if not self._starting:
                self.btn_server.setEnabled(True)
                self.btn_server.setText("Start Server")

    # ── Fenster ─────────────────────────────────────────────────────────────
    def _set_checked(self, cb: QCheckBox, value: bool) -> None:
        cb.blockSignals(True)
        cb.setChecked(bool(value))
        cb.blockSignals(False)

    def moveEvent(self, event):
        super().moveEvent(event)
        self.state["panel_x"] = self.x()
        self.state["panel_y"] = self.y()

    def closeEvent(self, event):
        # Nur ausblenden - beendet wird ueber das Tray-Icon.
        event.ignore()
        self.hide()
        self.state["panel_visible"] = False
