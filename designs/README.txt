KERS Subsystems – eigenes Design
================================

Dieser Ordner ist ein Design für das Desktop-HUD (und das OBS-Fenster). KERS
findet ihn unter  data\designs\<Ordnername>  neben KERS_Subsystems.exe.
Auswählen: /settings → Design. Nach einer Änderung an den Dateien im
Schaltbrett „Neu laden“ drücken – dann ist sie sofort zu sehen.

⚠ Ein Design enthält QML, also Programmcode. Installiere nur Designs von
  Leuten, denen du vertraust.


Aufbau
------

  design.json     Name, Farben, Deckkraft, Formen, Schriften (siehe unten)
  fonts\          eigene Schriften (.ttf/.otf) – werden beim Wechsel geladen
  images\         eigene Bilder (frei verwendbar aus deinen QML-Dateien)
  qml\            die Szene. Jede Datei hier ERSETZT die gleichnamige
                  eingebaute Datei; was fehlt, kommt aus dem Standard-Design.
                  Beim Kopieren liegt die ganze Szene hier – löschen, was du
                  nicht ändern willst, schadet nicht.

Die wichtigsten Dateien in qml\:
  Overlay.qml            ordnet alle Bausteine auf dem Bildschirm an
  parts\Tower.qml        der Timing Tower   (parts\TowerRow.qml = eine Zeile)
  parts\Hotlaps.qml      Quali-Karten unten
  parts\Battles.qml      Battle-Boxen       (parts\BattleRow.qml)
  parts\RaceMessage.qml  Meldungen der Rennleitung
  parts\FastestLapBanner.qml, parts\Championship.qml, parts\LowerThird.qml,
  parts\Onboard.qml, parts\Trackmap.qml, parts\StartLights.qml ...
  Fmt.js                 Zeit- und Farbformate

Neue Dateien sind erlaubt (z. B. parts\MeineZeile.qml) – sie sind im selben
Ordner automatisch als Typ verfügbar.


design.json
-----------

  "name", "author", "description"   erscheinen in /settings

  "colors"   Farben als #RRGGBB
     accent        Akzent (wird von der Akzentfarbe in /settings ersetzt,
                   sobald du dort eine andere als #e10600 wählst)
     textMain, textMuted
     panel         Grund der Panels
     row, rowAlt   Zeilen im Tower (abwechselnd)
     headerRow     Spaltenkopf im Tower
     towerHead1/2  Verlauf im Tower-Kopf
     headBg1/2     Verlauf in den Kartenköpfen (Battles, Hotlaps …)
     panelBorder   Rahmen

  "alpha"    Grund-Deckkraft je Fläche (0 … 1). Wird noch mit der Deckkraft
             aus /settings multipliziert. panelBorder gilt unverändert.

  "shape"    panelRadius (Ecken), accentWidth (Akzentlinien), rowHeight
             (Zeilenhöhe des eingebauten Towers)

  "fonts"    sans / display: ein Schriftname oder eine Liste – die erste
             vorhandene gewinnt, z. B. ["Formula1 Display", "Titillium Web"].
             Fehlt eine, bleibt es bei Inter bzw. Teko.

  "extra"    beliebige eigene Werte. In QML als  Theme.extra.<name>  lesbar,
             z. B. "posBg": "#14151a"  →  Theme.extra.posBg


In QML verfügbar
----------------

  Theme   alle Farben oben (Theme.accent, Theme.panelBg, Theme.rowBg …),
          Theme.panelRadius, Theme.sans, Theme.display, Theme.extra,
          Theme.teamLogo(name), Theme.tyre(c), Theme.brandLogo(datei)
  Kers    die Live-Daten: Kers.drivers (Fahrerliste), Kers.session
          (isQuali, currentLap, totalLaps, timeLeft, sessionName, scStatus,
          redFlag, poleTime …), Kers.settings, Kers.hotlaps, Kers.battles,
          Kers.championship …
  Am einfachsten: die eingebaute Datei als Vorlage nehmen und umbauen.
