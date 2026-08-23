# KERS Subsystems

Live-Telemetrie-Overlay für **F1 26** — als Fenster über dem Spiel und als Quelle für OBS.

Das Programm hört auf den UDP-Strom des Spiels, rechnet daraus Abstände, Reifen,
Strafen und Kampfgruppen aus und zeigt sie als Bausteine an: Timing Tower,
Trackmap, Onboard, Lower-Third, Battle-Boxen, Boxenstopp-Karten, WM-Stand und
mehr. Jeder Baustein lässt sich einzeln abschalten und frei platzieren.

> Nur **F1 26** (UDP-Format „2026"). Ältere Spiele senden ein anderes Format und
> werden nicht gelesen.

---

## Schnellstart

1. Unter [Releases](https://github.com/KERSEX/KERS_Overlay/releases) die
   `KERS_Subsystems.exe` herunterladen und in einen eigenen Ordner legen.
2. Im Spiel unter **Einstellungen → Telemetrie** die UDP-Ausgabe einschalten:
   * UDP Telemetry: **On**
   * UDP Format: **2026**
   * IP: `127.0.0.1`, Port: `20777`
   * Fahrer `Spielernamen: AN`
3. `KERS_Subsystems.exe` starten. Es öffnet sich das Schaltbrett *KERS Subsystems*.
4. Dort **Start Server** drücken, dann **HUD IST AN**.

Beim ersten Start legt die EXE daneben einen Ordner `data\` an. Dort liegen alle
deine Daten — Einstellungen, Fensterlage, WM-Stände, Aufzeichnungen. Die EXE
selbst enthält keine persönlichen Daten.

---

## Die beiden Fenster

**Das HUD** ist ein rahmenloses Fenster, das immer über allem liegt.
Im gesperrten Zustand gehen alle Klicks hindurch — du kannst also normal spielen,
während es über dem Bild liegt. Entsperrt lässt es sich verschieben und in der
Größe ändern.

**Das Schaltbrett** ist die Steuerung: Server starten und stoppen, HUD an und aus,
sperren, auf den Bildschirm legen, Deckkraft, Renderer und Layout.
Es hat außerdem ein Symbol im Infobereich der Taskleiste.

---

## Bedienung im Browser

Läuft der Server, erreichst du diese Seiten unter `http://localhost:5100`
(vom Handy im selben WLAN über die IP des Rechners):

| Seite | Wofür |
|---|---|
| `/` | Das komplette Overlay als Browserquelle |
| `/regie` | Regie-Panel: Charts, WM-Stand, Battle- und Hotlap-Boxen von Hand ein- und ausblenden |
| `/settings` | Alle Einstellungen: Branding, Farben, Deckkraft, welche Bausteine sichtbar sind, Layout |

---

## Layout — Bausteine frei platzieren

Seit 0.2.0 bestimmst du selbst, wo jeder der 13 Bausteine sitzt.

**Mit der Maus:** im Schaltbrett auf **Layout bearbeiten**. Das HUD entsperrt sich
dabei selbst, alle Bausteine bekommen einen Rahmen mit Namen, und du ziehst sie an
ihren Platz. An den Ecken ziehst du sie größer und kleiner. Was gerade nichts
anzeigt (die Hotlap-Boxen laufen nur in der Quali) bekommt einen beschrifteten
Platzhalter, damit es trotzdem greifbar ist. Beendet wird oben mit **Fertig** oder
mit **Esc** — nicht im Schaltbrett, denn das liegt währenddessen unter der
bildschirmgroßen Klickfläche des HUD.

**Auf den Pixel genau:** in `/settings` gibt es zu jedem Baustein Regler für
Ankerpunkt, Versatz, Ebene und Größe.

**Was liegt vorn:** unter den Bausteinen steht die Liste **Ebenen** — oben liegt
vorn, mit den Pfeilen sortierst du um. Das ändert wirklich nur die Ebene: ein
Baustein, der noch auf *Standard* steht, behält seinen eingebauten Platz und
rechnet weiter mit (der Tower folgt der Strafen-Seite, die Trackmap ihrer
Position, die Pit-Projektion weicht dem Onboard aus).

**Mehrere Anordnungen:** unter **Eigene Layouts** sicherst du die aktuelle
Anordnung unter einem Namen und holst sie mit einem Klick zurück — z.B. eine fürs
Rennen und eine für die Quali. Gesichert wird nur das Layout: Ankerpunkt, Versatz,
Ebene und Größe je Baustein. Farben, Deckkraft und Schalter bleiben unberührt,
dafür sind die *Presets* ganz oben da. Die Layouts liegen auf dem Server
(`layouts.json` neben der .exe), sind also auch vom Handy aus da.

**Zurück auf Anfang:** der Knopf **Standard Layout** neben *Layout bearbeiten*
stellt alle Bausteine wieder an ihren eingebauten Platz. Das betrifft nur die
Positionen — Branding, Farben und der Rest bleiben. Nur die Ebenen zurücksetzen
geht in `/settings` mit **Ebenen auf Standard**; Positionen und Größen bleiben
dabei stehen.

Ein Baustein hängt dabei nicht an festen Koordinaten, sondern an einem von neun
Ankerpunkten plus Versatz. Was du unten rechts platzierst, bleibt unten rechts,
auch wenn das Fenster eine andere Größe hat — praktisch, weil das HUD meist in
2560×1440 läuft und OBS in 1920×1080 aufnimmt.

---

## OBS

Zwei Wege:

* **Fensteraufnahme.** Das HUD kann ein zweites, undurchsichtiges Fenster für die
  Aufnahme öffnen. Damit OBS es überhaupt findet, im Schaltbrett *In OBS auffindbar*
  einschalten — sonst blendet OBS das Fenster aus.
* **Browserquelle.** `http://localhost:5100/` mit transparentem Hintergrund, oder
  einzelne Bausteine über `/part/<name>`.

Reicht deine Aufnahmemethode den Alphakanal nicht durch, gibt es die Rückfallebene
*Chroma*: das Overlay bekommt einen einfarbigen Hintergrund (Magenta, weil die
Farbe in einem Formel-1-Overlay nicht vorkommt), den du in OBS per Farbschlüssel
wegfilterst.

---

## Selbst-Update

Beim Start sieht das Programm still nach, ob es eine neuere Fassung gibt, und
meldet sich mit einem Knopf. Der Download wird gegen Größe und SHA-256 geprüft,
bevor er die laufende Datei ersetzt; die alte bleibt als Rückfallebene liegen.
Du kannst im Schaltbrett auch gezielt eine andere Fassung wählen.

---

## Aus dem Quelltext starten

Gebraucht wird **Python 3.13** (damit wird entwickelt und gebaut).

```bat
run.bat
```

Legt beim ersten Mal eine `venv` an, installiert `requirements.txt` und startet
den Server auf Port 5100. Danach ist das Overlay im Browser erreichbar.

```bat
start_hud.bat
```

Startet das Desktop-HUD samt Schaltbrett. Dass sich das Konsolenfenster gleich
wieder schließt, ist normal — das HUD läuft ohne eigene Konsole weiter. Den Server
kannst du danach im Schaltbrett per Knopf starten.

Einmalig für das QML-Overlay: `python tools\woff2_to_ttf.py` erzeugt die
TTF-Schriften. Qt kann keine woff2-Dateien lesen.

Abhängigkeiten: `flask`, `requests` für den Server, `PySide6` zusätzlich für das
HUD (bringt QtWebEngine mit, daher der größere Download beim ersten Mal).

---

## Aufbau

```
main.py                 Flask-Server + UDP-Parser (F1 26 / Format 2026)
templates/, static/     Das Overlay als Webseite, Regie und Einstellungen
livery.py               Team- und Fahrerfarben
championship.json       WM-Stand
hud/
  kers_hud.py           Startpunkt des Desktop-HUD
  subsystems_panel.py   Das Schaltbrett
  qml_overlay.py        Das Always-on-Top-Fenster
  qml/                  Die Overlay-Szene
  bridge.py             Server -> Szene
  derive.py, parts.py   Die Rechnerei hinter den Bausteinen
  updater.py            Selbst-Update über GitHub Releases
  README.md             Ausführliche Technik-Notizen zum HUD
tools/                  Hilfsskripte
```

Ports: **5100** für die Weboberfläche, **20777** für die UDP-Pakete des Spiels.

> Beim Testen darf nur **eine** Quelle auf 20777 senden — entweder das echte Spiel
> oder ein Testsender. Sonst mischen sich die Daten.

---

## Fehlersuche

**Nichts kommt an.** Prüfe im Schaltbrett die Statuszeile: sie zeigt getrennt, ob
der Server läuft und ob UDP-Pakete ankommen. Sendet das Spiel ein anderes Format
als 2026, kommen Pakete an, werden aber nicht gelesen.

**Das HUD liegt hinter dem Spiel.** Im randlosen Vollbild schiebt sich das Spiel
manchmal nach vorn. Das HUD meldet sich alle zwei Sekunden von selbst wieder oben
an; hilft das nicht, ist meist das Spiel im echten Vollbild — dann ist die
Browserquelle in OBS der bessere Weg.

**OBS findet das Fenster nicht.** *In OBS auffindbar* einschalten. Dafür erscheint
das HUD dann auch in der Taskleiste.

---

## Lizenz und Zugehörigkeit

Privates Projekt, kein offizielles Produkt von EA oder Codemasters. Alle Marken
gehören ihren jeweiligen Eigentümern.
