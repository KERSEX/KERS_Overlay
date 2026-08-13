# KERS HUD — Overlay als Desktop-Fenster

> **Das Overlay ist von HTML nach QML umgezogen.** Der QML-Renderer ist die Vorgabe,
> **alle 15 Bausteine sind portiert**. Die HTML-Seiten bleiben vorerst bestehen und
> sind im Schaltbrett unter „Renderer" weiter erreichbar — zum Vergleichen und als
> Rückfallebene. Siehe [Umbau auf QML](#umbau-auf-qml).

> **Wichtigster Befund zum Speicherverbrauch — siehe [Sparfassung](#sparfassung-opti) unten.**
> Die 2–3 GB kamen von zwei `requestAnimationFrame`-Dauerschleifen im Overlay-HTML.
> Gemessen: `/test` wächst um **86 MB/min (≈ 5 GB/Stunde, kein Plateau)**, `/opti` bleibt
> flach bei ~313 MB. Das betrifft **auch OBS**, nicht nur das HUD.


Ordner 3 = die vollständige NG-Version aus Ordner 1 **plus** das HUD aus Ordner 2.
Am Server (`main.py`), an `templates/` und an `static/` wurde **nichts** geändert —
das HUD hängt nur als weiterer Client dran.

## Was dazugekommen ist

```
3/
  start_hud.bat          Starter fürs HUD
  requirements.txt       + PySide6
  tools/
    woff2_to_ttf.py      erzeugt static/fonts/ttf/ — Qt kann kein woff2 (einmalig)
  hud/
    kers_hud.py          Startpunkt: HUD-Fenster + Subsystems + Tray-Icon
    subsystems_panel.py  das Schaltbrett
    api_client.py        fragt /api/status ab (ob Server und UDP leben)
    common.py            gespeicherter Zustand, Icon, Seitenliste
    hud_state.json       Position/Größe/Deckkraft/Zoom — wird beim ersten Start angelegt

    overlay_window.py    Renderer "web": das Always-on-Top-Fenster mit WebEngine
    qml_overlay.py       Renderer "qml": dasselbe Fenster mit einer QQuickView
    obs_window.py        das zweite, undurchsichtige Fenster für die Aufnahme
    bridge.py            Server -> QML: hält Modelle und Zustände
    feed.py              SSE-Client gegen /api/stream (mit Polling-Rückfall)
    derive.py            Portierung von static/js/core.js (gemeinsame Buchhaltung)
    models.py            Fahrer-Modell + Session-/Settings-/Regie-Zustand
    parts.py             Zustand der Bausteine (Portierung von static/parts/*.js)
    extras.py            Trackmap, Charts, WM-Stand (eigene Server-Abfragen)
    theme.py             Portierung von :root in static/css/core.css
    demo.py              erfundenes Rennen für die Arbeit ohne Spiel
    qml/                 die Szene
      Overlay.qml          Wurzel: hier hängen alle Bausteine an ihrem Platz
      EditFrame.qml        Bearbeiten-Rahmen (ersetzt EditGlass)
      Fmt.js               Zeit-/Gap-/Farbformate
      MedalText.qml        Podiumszahlen mit Metallverlauf
      MaskedIcon.qml       PNG als Form, frei einfärbbar (Damage-Icons)
      RunningBorder.qml    umlaufender Lichtpunkt (VSC-Rahmen, MOM-Pille)
      parts/               ein QML je Baustein, plus Tower/BattleRow als Zeilen
```

## Starten

```bash
start_hud.bat
```

Den Server kannst du danach direkt im Schaltbrett per Knopf starten — oder wie bisher
mit `run.bat`. Beim allerersten Mal muss `run.bat` einmal laufen, weil es die `venv`
anlegt und `requirements.txt` installiert (jetzt inkl. PySide6, ~80 MB, einmalig).

Optionen: `--qml` / `--web` (Renderer), `--demo` bzw. `--demo quali` (erfundenes Rennen
ohne Server), `--obs` (Aufnahmefenster gleich mit öffnen), `--chroma`, `--unlocked`,
`--no-panel`, `--page /test` (Spielwiese, nur `--web`), `--no-gpu` (falls der
Hintergrund schwarz statt durchsichtig bleibt).

```bash
start_hud.bat --demo
```

Der Demo-Modus erzeugt ein vollständiges Rennen aus dem Nichts: 22 Fahrer (11 Teams
wie ab 2026, inklusive Cadillac), die sich überholen, Boxenstopps, Schaden, Strafen,
Rennleitungs-Meldungen, Kamerawechsel, eine erfundene Strecke für die Trackmap und im
Wechsel Safety Car, VSC und rote Flagge. Damit lässt sich am Overlay arbeiten, ohne
auf ein echtes Rennen zu warten — und ohne dass `main.py` überhaupt läuft.

**Die echten Settings werden mitgelesen.** Gibt es `overlay_settings.json` neben
`main.py`, legt der Demo-Modus sie über seine Vorgaben. Branding, Logo, Deckkraft und
Skalierung sehen also genauso aus wie im Betrieb — man kann sein Logo damit vorschauen,
ohne den Server zu starten.

Grober Fahrplan eines Durchlaufs (Sekunden ab Start):

| | |
|---|---|
| 0–9 | Startampel, dann „Lights Out" |
| ab 20 | Battle-Boxen (ab Runde 3) |
| 55–82 | Gap- und Rundenzeiten-Chart |
| 70–140 | Safety Car / VSC → Onboard und gelbe Streckenabschnitte |
| ab 88 | Boxenstopps, Undercut-Meldungen, Pit-Projektion |
| 100–125 | WM-Stand |

Mit `--demo quali` läuft stattdessen ein Qualifying: Hotlap-Boxen, Sektorfarben,
Elimination-Linie und die Gefahrenzone in den letzten drei Minuten (die Quali-Uhr
läuft im Demo-Modus in 220-Sekunden-Runden, damit man nicht eine Viertelstunde
darauf warten muss).

Zwei Dinge kann der Demo-Modus nicht: den **Positionsverlauf** (der kommt aus
`/api/lap_positions`, also vom Server) und alles, was von echten UDP-Daten abhängt.
Streckenkontur und WM-Stand liefert `demo.py` dagegen selbst mit.

## Umbau auf QML

Der Renderer `qml` zeigt dieselben Daten wie das Web-Overlay, gezeichnet direkt über
die Grafikkarte statt durch eine eingebettete Browser-Engine. Kein Chromium, kein
zweiter Prozess, echter Alphakanal, kein DOM.

**Was der Unterschied für die Bedienung bedeutet:** Im Web-Renderer hängt EINE Seite
im Fenster (`/`, `/opti`, `/part/tower` …) — deshalb gibt es dort die Auswahl „Seite".
In QML sind alle Bausteine **eine** Szene; welche davon zu sehen sind, entscheiden wie
gewohnt die Schalter in `/settings`. Die Seitenauswahl ist im QML-Modus deshalb
ausgegraut.

### Woher die Daten kommen

Unverändert vom Server: `feed.py` hängt an `/api/stream`, genau wie `core.js` im
Browser. Der Server bleibt die einzige Wahrheit — QML und die HTML-Seiten zeigen
garantiert denselben Stand, solange beide nebeneinander laufen.

```
/api/stream  ->  feed.py (eigener Thread)
             ->  bridge.py  ->  derive.py    gemeinsame Buchhaltung (= core.js)
                            ->  models.py    Fahrerzeilen + Session/Settings
             ->  qml/                        zeichnet
```

`derive.py` und `theme.py` sind **absichtlich treue Portierungen** von `core.js` bzw.
dem `:root`-Block in `core.css`. Wo das Original eine Eigenheit hat — die Reihenfolge
von Einfärben und Verrechnen der Sektoren, die Toleranzen in `qualiStatus`, der
Sonderfall für S3 an der Start/Ziel-Linie — steht sie dort genauso drin. Ändert sich
etwas am Web-Overlay, muss es in beiden Dateien nachgezogen werden, solange beide
Renderer leben.

### Wo welcher Baustein gelandet ist

| Baustein | Vorlage | QML |
|---|---|---|
| Timing Tower | `tower.js` | `parts/Tower.qml`, `parts/TowerRow.qml` |
| Battle-Boxen | `battles.js` | `parts/Battles.qml`, `parts/BattleRow.qml` |
| Hotlap-Boxen (Quali) | `hotlap.js` | `parts/Hotlaps.qml` |
| Trackmap | `trackmap.js` | `parts/Trackmap.qml` |
| Onboard-Telemetrie | `onboard.js` | `parts/Onboard.qml` |
| Lower-Third | `lowerthird.js` | `parts/LowerThird.qml` |
| Boxenstopp-Timer | `pit.js` | `parts/PitCards.qml` |
| Start-Ampel | `lights.js` | `parts/StartLights.qml` |
| Fastest-Lap-Banner | `flbanner.js` | `parts/FastestLapBanner.qml` |
| Rennleitung + Undercut | `racemsg.js`, `undercut.js` | `parts/RaceMessage.qml` |
| Gefahrenzone (Quali) | `danger.js` | `parts/DangerZone.qml` |
| WM-Stand | `champ.js` | `parts/Championship.qml` |
| Pit-Projektion | `pitproj.js` | `parts/PitProjection.qml` |
| Verlaufs-Charts | `charts.js` | `parts/Charts.qml` |

Im Tower ist alles drin, was `tower.js` kann: Positionswechsel mit Gleitanimation und
Auf-/Ab-Pfeil, Podiumszahlen in Gold/Silber/Bronze mit Glanz-Sweep, Sektorfarben,
Strafen- und Track-Limits-Pillen, Damage-Icons, Comeback-Badge, Bestrunde-Flash,
Reifenwechsel-Animation, Zielflagge, DRS bzw. der 2026er Overtake-Mode mit
umlaufendem Lichtpunkt, sowie die automatische Skalierung auf die Fensterhöhe.

**Rennleitung und Undercut teilen sich einen Banner.** Das ist kein Versehen: in
`templates/index.html` gibt es genau ein `#race-msg`, und `undercut.js` schreibt in
dasselbe Element. Die getrennten Seiten `/part/racemsg` und `/part/undercut` gibt es
nur, damit man sie in OBS getrennt platzieren kann — im Gesamt-Overlay war es immer
ein Banner.

**Zwei Überlappungen sind aus dem Original übernommen**, nicht neu: der WM-Stand
(`bottom: 40, right: 24`) liegt auf demselben Fleck wie der Boxenstopp-Stapel
(`bottom: 28, right: 24`), und die Gefahrenzone (`bottom: 40`) auf demselben wie die
Hotlap-Boxen (`bottom: 28`). Beides steht so im CSS. Wenn es stören soll, sind das
zwei Zahlen in `Overlay.qml`.

### Drei Dinge, die beim Bauen Zeit gekostet haben

**Schriften.** Das Web-Overlay lädt Inter und Teko als **woff2** — das kann Qt nicht.
`tools/woff2_to_ttf.py` schneidet daraus feste TTF-Schnitte (Regular bis ExtraBold)
nach `static/fonts/ttf/`. Die liegen mit im Repo; das Skript muss nur laufen, wenn die
woff2 ausgetauscht werden. Wichtig dabei: eine Variable Font würde Qt nur mit ihrer
Standardinstanz führen, `font.weight: Font.Bold` wählte dann gar nichts aus.

**Keine QML-Singletons.** `qmlRegisterSingletonInstance()` ist in PySide6 6.11 unter
Windows kaputt: sobald ein Singleton registriert ist, verliert die Engine die
Typidentität aller aus Plugin-DLLs geladenen Module, und `QtQuick.Shapes` scheitert mit
`Cannot assign object of type "QQuickShapePath" to list property "data"`. Deshalb
kommen `Kers`, `Theme` und `Hud` als Context-Properties in die Szene
(`qml_overlay.py`, `CONTEXT_NAMES`).

**`maskEnabled` und `shadowEnabled` vertragen sich nicht** in einem `MultiEffect`:
zusammen kippt die Maske, und statt der Ziffer sieht man den vollen Verlaufskasten mit
der Ziffer als Loch. Die Schatten in `MedalText.qml` und `MaskedIcon.qml` sind deshalb
von Hand gebaut (eine zweite, dunkle Kopie darunter).

Dazu kommt eine Kleinigkeit im Start: der PySide6-Ordner muss per
`os.add_dll_directory()` in den DLL-Suchpfad, sonst laden die Effekt-Plugins nicht
(`prepare_qml_runtime()` in `qml_overlay.py`).

Und zwei Fallen, in die man bei den restlichen Bausteinen leicht wieder tappt:

**Ein `Repeater` kann keine `ShapePath` erzeugen** — das sind keine Items. Trackmap
und Charts blieben deshalb im ersten Versuch komplett leer, obwohl QML keinen Fehler
meldete. Beide zeichnen ihre Linien jetzt auf einen `Canvas`, der nur bei neuen Daten
neu malt (statisch bei der Kontur, alle drei Sekunden beim Chart).

**Keine `y`-Animation auf dem direkten Kind eines `Column`.** Die Animation nimmt dem
Positionierer die Koordinate weg, und dann liegen alle Elemente auf `y = 0`
übereinander. Bei den Boxenstopp-Karten sah man das daran, dass hinter „HADJAR" noch
„ANTONELLI" hervorschaute. Lösung: eine Hülle, die der `Column` positioniert, und die
Animation auf dem Inhalt darin.

### Logos wirken körnig

Sah nach zu niedriger Deckkraft aus, war aber Filterung. Nachgemessen: das hellste
Pixel des Marken-Logos kam im Overlay mit `rgb(249,252,255)` an, in der Quelldatei mit
`rgb(248,248,248)` — es ist also voll deckend.

Die Ursache sind die unterschiedlichen Seitenverhältnisse der Team-Logos. Ferrari ist
149 × 202 und wird kaum verkleinert; **Aston Martin ist 256 × 58 und Cadillac
248 × 94** — die passen nur über die *Breite* in die 44-px-Zelle und schrumpfen dabei
auf etwa ein Sechstel. Bei so viel Verkleinerung greift die normale bilineare
Filterung daneben (sie liest nur 2 × 2 Texel) und das Logo wird körnig.

Deshalb steht auf allen Logo-`Image`s jetzt `mipmap: true` — damit rechnet Qt vorher
passende Verkleinerungsstufen aus — und `sourceSize` ist auf die tatsächliche
Anzeigegröße gesetzt statt auf einen festen Wert.

### Was bewusst NICHT übernommen wurde

Der `backdrop-filter` (Milchglas hinter den Panels). Der zeichnet weich, was
*innerhalb derselben Seite* hinter einem Element liegt. In einem Fenster mit echtem
Alphakanal liegt dort der Desktop bzw. in OBS gar nichts — es gäbe nichts zu blenden.
Im Web-Overlay war er aus demselben Grund schon wirkungslos (steht so auch weiter
unten unter „Sparfassung").

Drei Schriftgrößen sind gerundet: das CSS hat 13.5 px, 14.5 px und 28.8 px,
`font.pixelSize` ist in Qt aber ganzzahlig. Bei der Tower-Skalierung von 0,56 bis 1,12
liegt der Unterschied deutlich unter einem Bildpunkt.

## Overlay in OBS holen

**Kurzfassung: OBS-Fenster einschalten, in OBS per Fensteraufnahme holen
(`KERS OBS`), Farbschlüssel-Filter auf Magenta. Fertig.**

### Warum es ein zweites Fenster gibt

Das naheliegende — einfach das HUD-Fenster aufnehmen, das ohnehin auf dem Desktop
liegt — **funktioniert nicht**. Man findet es zwar (siehe unten), aber die
Fensteraufnahme zeigt dann genau ein Bild und friert darauf ein.

Das ist kein Fehler in OBS und keiner im Overlay, sondern eine Folge davon, wie ein
durchsichtiges Fenster unter Windows entsteht: Qt setzt dafür `WS_EX_LAYERED` und
lässt den Inhalt über DirectComposition unmittelbar in den Desktop zeichnen. Die
Windows Graphics Capture, aus der OBS seine Fensteraufnahme speist, liest dagegen
die DWM-Umleitungsfläche des Fensters — und die bekommt bei so einem Fenster keine
neuen Bilder mehr.

An **einem** Fenster ist das nicht lösbar: entweder es ist durchsichtig (dann taugt
es fürs Overlay auf dem Desktop, aber nicht für die Aufnahme) oder undurchsichtig
(dann umgekehrt). Also gibt es beides — dieselbe Szene, dieselbe Datenquelle, nur
zweimal gezeichnet:

| | |
|---|---|
| **KERS HUD** | rahmenlos, durchsichtig, immer oben, Klicks gehen durch. Fürs Auge beim Fahren. |
| **KERS OBS** | ein ganz normales Fenster in Canvas-Größe, undurchsichtig, Hintergrund in der Schlüsselfarbe. Für die Aufnahme. |

Das Desktop-HUD darf dabei ruhig **aus** bleiben — das OBS-Fenster läuft unabhängig.

### Einrichten

1. Im Schaltbrett unter **OBS** → **„Eigenes OBS-Fenster"** einschalten
   (oder `start_hud.bat --obs`, oder im Tray-Menü). Darunter die **Canvas**-Größe
   auf das stellen, was in OBS eingestellt ist — meist 1920 × 1080.
2. In OBS: **Fensteraufnahme** → Fenster `[pythonw.exe]: KERS OBS`,
   Methode „Windows 10 (1903 und neuer)", „Fensterrahmen aufnehmen" **aus**.
3. Auf die Quelle einen Filter **Farbschlüssel** legen, Schlüsselfarbe **Magenta**.

Der Chroma-Key ist hier kein Notbehelf, sondern der Plan: das OBS-Fenster ist
absichtlich undurchsichtig, und **alle Panels sind darin deckend**. Das Zweite ist
der eigentliche Trick — ohne das schiene die Schlüsselfarbe durch die zu 94 %
deckenden Panels, der Farbschlüssel fräße sie anteilig mit weg und das Overlay
bekäme einen Farbstich. Auf dem Desktop-HUD bleiben die Panels durchsichtig; beide
Fenster haben ihr eigenes Theme.

Magenta ist voreingestellt, weil es im Overlay garantiert nirgends vorkommt — Grün
kollidiert mit den Sektorfarben und dem DRS-Grün, Blau mit Alpine, Williams und RB.
Grün und Blau stehen trotzdem zur Auswahl.

> Nachgemessen statt geraten: das OBS-Fenster kommt mit den erweiterten Stilen
> `0x00000100` heraus — **kein** `WS_EX_LAYERED`, **kein** `WS_EX_TOOLWINDOW` —,
> steht in einer Nachbildung von OBS' eigener Fensterliste, und zwei Aufnahmen im
> Abstand von 2,5 s unterscheiden sich. Das HUD-Fenster hat dagegen `0x000800A8`
> (`TOOLWINDOW | TOPMOST | LAYERED | TRANSPARENT`).

### Doch lieber direkt das HUD-Fenster?

Möglich, aber mit den genannten Einschränkungen. Zwei Schalter dafür:

**„HUD-Fenster für OBS auffindbar machen"** — ohne das listet OBS es gar nicht erst
auf. Das HUD läuft als Werkzeugfenster (`Qt.Tool`), damit es keinen Taskleisten- und
Alt-Tab-Eintrag hat; Windows setzt dafür `WS_EX_TOOLWINDOW`, und **genau den wirft
OBS aus seiner Fensterliste** (`win-capture/window-helpers.c`,
`check_window_valid`). Der Schalter nimmt den Stil weg, der Preis ist der
Taskleisten-Eintrag — beides hängt an derselben Eigenschaft.

> `setFlags()` allein reicht dafür nicht: Qt ändert den nativen Stil eines bereits
> erzeugten Fensters nicht mehr (nachgemessen, der Wert blieb unverändert). Der
> Schalter geht deshalb direkt über die Win32-API
> (`_apply_native_toolwindow()` in `qml_overlay.py`).

**„HUD-Fenster einfarbig statt durchsichtig"** — dasselbe wie beim OBS-Fenster, nur
am HUD selbst. Dann ist allerdings auch auf dem Desktop nichts mehr durchsichtig.

Mit **Spieleaufnahme** → „Bestimmtes Fenster aufnehmen" → `KERS HUD` →
„Transparenz zulassen" lässt sich das HUD-Fenster prinzipiell auch mit echtem Alpha
holen: die Spieleaufnahme hängt sich in die D3D-Swapchain und geht damit an der
DWM-Umleitung vorbei. Ob der Hook greift, hängt aber am System — deshalb ist es hier
nicht der empfohlene Weg.

> Alle OBS-Schalter gelten nur für den QML-Renderer. Im Web-Renderer bleibt es bei
> den Browser-Quellen auf `/part/<name>`.

## Die drei Fenster

**HUD-Fenster** — rahmenlos, immer oben, durchsichtiger Hintergrund. Zeigt deine
normale Overlay-Seite. Gesperrt gehen Klicks durch zum Spiel, entsperrt hat es einen
roten Rahmen und lässt sich ziehen und an den Ecken skalieren (Doppelklick = sperren).

**KERS Subsystems** — das Schaltbrett, von oben nach unten:

| | |
|---|---|
| Statuszeile + **Server starten / Server stoppen** | `main.py` in einem eigenen Konsolenfenster |
| großer Hauptschalter | HUD an/aus (grün = an) |
| **Bildschirm füllen** / Zentrieren / Neu laden | |
| HUD-Fenster | sperren, **Renderer**, Seite, Overlay-Hz, X/Y/Breite/Höhe, Fenster immer vorn |
| **OBS** | eigenes OBS-Fenster + Canvas-Größe, Schlüsselfarbe; darunter die zwei Schalter fürs HUD-Fenster selbst |
| Regie öffnen / Settings öffnen | öffnen im Browser |
| **ALLES BEENDEN** | Server beenden, HUD schließen, Subsystems beenden |

**Tray-Icon** — die Notbremse, wenn das HUD gesperrt und das Schaltbrett zu ist:
Linksklick = sperren/entsperren, Rechtsklick = Menü mit denselben Funktionen
(inkl. Server starten/stoppen und Bildschirm füllen), „Alles beenden (mit Server)"
und „Nur HUD beenden".

## Was hier NICHT reingehört

Subsystems macht nur das, was es sonst nirgends gibt: **Server starten** und das
**HUD-Fenster bedienen**. Alles andere bleibt da, wo es schon gut ist:

| | wo |
|---|---|
| Bausteine an/aus, Regler, Branding, Presets, Trackmap | `/settings` |
| manuelle Einblendungen (Charts, Strategie, WM-Stand) | `/regie` |

Beide sind unten im Schaltbrett einen Knopfdruck entfernt. Das HUD fasst
`/api/settings` und `/api/regie` deshalb überhaupt nicht an — es gäbe sonst zwei
Wahrheiten für dieselbe Einstellung.

**Deckkraft und Zoom** haben bewusst keine Regler mehr. Sie werden beim Start aus
`hud_state.json` übernommen (`"opacity"` in Prozent, `"zoom"` als Faktor) — dort
änderst du sie, wenn überhaupt.

> ⚠ Es gibt **zwei** Deckkräfte, und sie tun Verschiedenes:
>
> | wo | was sie färbt |
> |---|---|
> | `/settings` → `opacity` | nur die **Panelflächen** (`Theme.uiAlpha`). Texte, Logos und Bilder bleiben voll deckend. |
> | `hud_state.json` → `"opacity"` | das **ganze Fenster** inklusive Logos. Steht auf 100 und sollte da auch bleiben. |
>
> Wirkt ein Logo trotzdem matt, liegt es fast immer am Verkleinern, nicht an der
> Deckkraft — siehe „Logos wirken körnig" weiter unten.

Fenster zumachen beendet nichts, es blendet nur aus.

## Server starten und stoppen

**Starten** führt `main.py` mit dem venv-Python in einem **eigenen Konsolenfenster** aus —
bewusst sichtbar, nicht still im Hintergrund: dort landen die Meldungen, und genau die
willst du sehen, wenn z. B. UDP 20777 schon von `testgui.py` oder einer zweiten Instanz
belegt ist. Der Knopf ist gesperrt, solange der Server erreichbar ist, damit nicht
versehentlich eine zweite Instanz startet.

**Stoppen** ist zweistufig, weil `main.py` keinen Endpunkt zum Herunterfahren hat — der
Prozess muss also beendet werden:

| Fall | was passiert |
|---|---|
| von hier gestartet | wird sofort beendet (`taskkill /T`, das Konsolenfenster hängt mit dran) |
| fremd gestartet (run.bat, eigenes Terminal) | wird über den Port gesucht, Rückfrage mit PID, erst dann beendet |
| Prozess auf dem Port ist kein Python | Warnung, es wird **nichts** beendet |

Die Rückfrage beim fremden Prozess ist Absicht: erkannt wird er nur daran, dass er auf
Port 5100 lauscht — theoretisch könnte da etwas anderes sitzen.

**ALLES BEENDEN** macht beides auf einmal: eine Rückfrage (die sagt, was genau dran
glauben muss), dann Server beenden, HUD schließen und Subsystems beenden. Läuft kein
Server, wird nur das HUD beendet.

> Die Port-Suche liest `netstat -ano` und wertet die Statusspalte **bewusst nicht** aus:
> auf deinem deutschen Windows steht dort „ABHÖREN" statt „LISTENING". Erkannt wird ein
> lauschender Socket stattdessen daran, dass die Gegenstelle Port 0 hat — das ist in jeder
> Sprache gleich.

## „Bildschirm füllen" sperrt mit Absicht

Der Knopf legt das HUD auf die volle Fläche des Bildschirms, auf dem es gerade steht
(Mehrmonitor: der, auf dem das Fenster liegt) — und **sperrt es dabei automatisch**.

Das muss so: entsperrt liegt über dem HUD die Bearbeiten-Fläche, die alle Mausklicks
abfängt. Bildschirmfüllend würde die über allem liegen — du kämst weder ans Spiel noch
ans Schaltbrett heran und müsstest dich übers Tray-Icon befreien.

Zum Verschieben also erst entsperren (Checkbox oder Tray), dann ziehen.

## Sparfassung `/opti`

`templates/testopti.html` ist eine Kopie von `test.html`, auf Leistungsbedarf getrimmt.
Über „Seite" im Schaltbrett auswählbar, damit du `/test` und `/opti` im laufenden Betrieb
vergleichen kannst.

### Was gemessen wurde

Beide Seiten je 3 Minuten im echten HUD, gleiche Datenlast:

| | Speicher | Verlauf |
|---|---|---|
| `/test` | 319 → 562 MB | **+86 MB/min, linear, kein Plateau → ≈ 5 GB/Stunde** |
| `/opti` @30 Hz | 299 → 313 MB | steigt kurz, dann räumt der GC auf, danach **flach** |

Dazu: `/opti` bei 30 Hz rendert **2,4× häufiger** als `/test` (12,5 Hz) und braucht trotzdem
weniger. Die Renderrate war also nicht das Problem.

### Woran es lag

An zwei `requestAnimationFrame`-**Dauerschleifen** (`tickPitCards`, `tickHotlapTimes`). Die
laufen in `test.html` ab Seitenstart unbedingt für immer, mit der vollen Bildwiederholrate des
Monitors — bei 165 Hz also ~330 `querySelectorAll`-Aufrufe pro Sekunde, auch wenn null
Pit-Karten und null Hotlap-Boxen da sind, also fast immer.

Der Speicher wächst dadurch, weil eine Seite mit laufender rAF-Schleife **nie untätig** ist.
Chromium räumt seinen Müll aber nur in Leerlaufphasen auf — die kamen nie. In der Messung
sieht man den Moment, in dem `/opti` die Schleife beendet und der GC endlich drankommt: bei
92 s fällt der Verbrauch von 339 auf 313 MB und bleibt dort.

**Gegenprobe:** mit `?shadow=1&blur=1`, also Schatten und Weichzeichnung wieder an, bleibt
`/opti` trotzdem flach (+3 MB/min). Es lag am JavaScript, **nicht** am CSS.

> Das gilt genauso für OBS — dort läuft dieselbe Seite in derselben Browser-Engine. Der Fix
> gehört deshalb auch nach `test.html` und `index.html`, sobald du das Ergebnis abgenickt hast.

### Was in `/opti` anders ist

| Änderung | warum |
|---|---|
| ein bedarfsgesteuerter Ticker statt zwei Dauerschleifen | die Ursache des Speicherwachstums |
| Renderdrossel + `KERS.setHz(n)` | siehe „Overlay-Hz" unten |
| `?slim=1` am SSE-Stream | `settings`, `final_classification`, `quali_results` nur bei Änderung |
| kein `will-change` auf den Zeilen | hielt für alle 24 Zeilen dauerhaft eine GPU-Ebene vor |
| Schatten und `backdrop-filter` aus | mit `?shadow=1` / `?blur=1` zurückholbar |
| Trackmap-Polling wird nach dem Lernen langsam (2 s → 15 s) | `refreshTrack` tut danach nur noch bei Versionswechsel etwas |

Zum `backdrop-filter`: der zeichnet weich, was *hinter* dem Element liegt — **innerhalb
derselben Seite**. Im HUD kann er nichts bewirken, weil das Spiel ein anderes Fenster ist und
Chromium nicht dahinterschauen kann; hinter den Panels liegt nur der durchsichtige
Seitenhintergrund, und die Panels sind selbst zu 94 % deckend. Er kostete trotzdem jedes Bild
Arbeit. Nach der Messung ist das aber **Geschmackssache, keine Notwendigkeit** — er war nicht
die Ursache der 2–3 GB.

## Overlay-Hz

Auswahl im Schaltbrett: **10 / 20 / 30 / 60 / 120**. Wirkt sofort, ohne Neuladen, und gilt
**nur fürs HUD** — OBS und Handy bleiben bei voller Rate.

Chromium hat keinen brauchbaren „max fps"-Schalter, der Compositor zeichnet wenn sich etwas
ändert. Das Limit setzt deshalb an den drei Auslösern an:

1. `/api/stream?hz=N` — der Server pusht höchstens N-mal pro Sekunde an diesen Client
2. Renderdrossel — nur der neueste Payload wird gerendert, höchstens N-mal pro Sekunde
3. der Ticker für Pit-/Hotlap-Zeiten — statt voller Monitor-Rate

Gemessen (`KERS.renders()` in der Konsole): 10 → 9,6/s, 30 → 27,7/s, 120 → 58,3/s. Bei 120
greift die Obergrenze: **F1 sendet höchstens 60 Pakete/s**, darüber ändern sich die Daten nicht
mehr und die Rate wirkt nur noch auf die weich hochzählenden Zeiten.

## Messen

```bash
powershell -ExecutionPolicy Bypass -File hud\measure.ps1 -Seconds 300 -Label opti-30hz
```

Sampelt RAM und CPU von Server, HUD und allen Chromium-Prozessen in eine CSV und sagt am Ende,
ob der Verbrauch stabil ist oder wächst. Für einen fairen Vergleich immer dieselbe Last fahren
— am besten ein `.f1rec`-Replay über `testgui.py`.

Für den Blick ins Innere:

```bash
start_hud.bat --devtools
```

Danach `http://127.0.0.1:9222` im Browser öffnen: Performance-Monitor (JS-Heap, DOM-Knoten,
Layer-Anzahl), Memory-Snapshot, Rendering → „Layer borders".

## Zwei Fallstricke

**Vollbild.** Über einem exklusiven Vollbild kann Windows kein Fenster legen — das gilt
für jedes Overlay dieser Bauart, auch für Pits N' Giggles. F1 muss auf **Rahmenloses
Fenster** stehen.

**Ein Fenster, nicht mehrere.** Das HUD zeigt das Gesamt-Overlay. Im Web-Renderer lässt
sich das Fenster über „Seite" auch auf einen einzelnen Baustein setzen (`/part/tower`
usw.); im QML-Renderer sind alle Bausteine eine Szene und werden über `/settings`
ein- und ausgeschaltet. Für mehrere Bausteine gleichzeitig, jeder frei platzierbar,
bräuchte es mehrere Fenster. Das ist bewusst nicht gebaut.

## Wie das „immer oben" funktioniert

Dieselbe Flag-Kombination wie in Pits N' Giggles
(`apps/hud/ui/overlays/base/base_overlay.py`), zu finden in
`overlay_window.py` → `update_window_flags()`:

```python
flags = (Qt.WindowType.FramelessWindowHint      # kein Rahmen
         | Qt.WindowType.WindowStaysOnTopHint   # <- das eigentliche "immer oben"
         | Qt.WindowType.Tool)                  # kein Taskbar-/Alt-Tab-Eintrag
if locked:
    flags |= Qt.WindowType.WindowTransparentForInput   # Klicks gehen durch
```

Dazu `WA_TranslucentBackground` fürs Fenster und
`view.page().setBackgroundColor(Qt.transparent)` für Chromium — sonst malt der Browser
einen weißen Grund und dein `body { background-color: transparent }` verpufft.

Im QML-Renderer (`qml_overlay.py` → `update_window_flags()`) ist es dieselbe
Flag-Kombination. Die Transparenz kommt dort aber nicht aus CSS, sondern aus dem
Alphakanal des Fensters: `setColor(transparent)` plus ein Alphapuffer im
Surface-Format, der **vor** der QApplication angemeldet sein muss
(`prepare_qml_runtime()`).

Qt übersetzt `WindowStaysOnTopHint` unter Windows selbst nach `SetWindowPos(HWND_TOPMOST)`,
einen eigenen Win32-Aufruf gibt es nirgends.

## Zwei Stolpersteine im Code (falls du dran weiterbaust)

`setWindowFlags()` versteckt unter Windows das Fenster und vergisst die Geometrie —
deshalb wird in `update_window_flags()` vorher `geometry()` gemerkt und danach wieder
gesetzt, plus `show()`.

An den Spinboxen darf **kein** `QSizePolicy.Ignored` stehen, wenn sie je wieder in einer
`QScrollArea` mit abgeschaltetem Horizontal-Scrollbalken landen: das Layout dreht sich
dann endlos im Kreis und das Fenster friert beim Start ein.
