// Die Wurzel der Overlay-Szene.
//
// Hier haengen alle Bausteine drin, jeder an seinem festen Platz - das Gegenstueck
// zu templates/index.html, wo jeder Baustein per `position: fixed` sitzt. Die
// Randabstaende unten sind dieselben Zahlen wie im CSS.
//
// Im Web gibt es zusaetzlich die Einzelseiten /part/<name> fuer eigene
// OBS-Browserquellen; hier ist alles EINE Szene, und was sichtbar ist, entscheiden
// die Settings (Kers.settings.showTower und Geschwister) sowie die Regie.

import QtQuick
import "parts"

Item {
    id: root

    // Kein eigener Hintergrund: die Durchsichtigkeit macht das Fenster selbst
    // (setColor in qml_overlay.py). Im Chroma-Key-Modus setzt Python dort die
    // Schluesselfarbe ein - auch dann braucht es hier nichts.

    // ── Timing Tower: oben links ────────────────────────────────────────────
    Tower {
        // Der linke Abstand existiert NUR, um die Strafen-Pillen aufzufangen, die
        // links aus der Zeile herausragen (im CSS: padding-left am body, dort 72,
        // hier auf Wunsch um ein Drittel verringert auf 48).
        //
        // Stehen die Pillen rechts (Setting penside), braucht es links gar keine
        // Reserve mehr - dann reicht derselbe Randabstand wie bei der Trackmap.
        // Der Tower rueckt damit von selbst nach links, sobald du umstellst, und
        // wieder zurueck, wenn du es rueckgaengig machst.
        x: Kers.settings.penSide === "right" ? 12 : 48
        y: 10
        stageHeight: root.height
        z: 30
    }

    // ── Trackmap: Platz aus den Settings (mapcorner) ────────────────────────
    // Bis 08/2026 hing sie fest oben rechts - die Auswahl in /settings war im
    // QML-Renderer damit wirkungslos (im Web arbeitet applyMapCorner()).
    // Nicht angeboten werden "oben links" und "links mitte": dort steht der Tower.
    //
    // ⚠ Bewusst x/y statt umschaltbarer anchors: ein Anker laesst sich mit
    // `undefined` NICHT verlaesslich wieder abschalten. Beim ersten Versuch hing
    // die Karte dadurch gleichzeitig an linker und rechter Kante und wurde auf
    // Fensterbreite gezogen - statt 480 px war sie ueber 2000 px breit. Mit x/y
    // bleibt `width: src.size` aus Trackmap.qml unangetastet.
    Trackmap {
        id: trackmap
        z: 42

        // Unbekannte Werte (z.B. das abgeschaffte "tl" aus einer aelteren
        // overlay_settings.json) auf "tr" biegen.
        readonly property string corner: {
            const c = Kers.settings.mapCorner || "tr";
            return ["tc", "tr", "rc", "bl", "bc", "br"].indexOf(c) >= 0 ? c : "tr";
        }
        // Abstand zur Bildschirmkante. Dazu kommt der Rand INNERHALB der Karte
        // (~29 px bei 480 px, s. FIT_TARGET in extras.py) - zusammen ergibt das den
        // sichtbaren Abstand. Kleiner = naeher an die Kante.
        readonly property int gap: 12

        x: corner === "bl" ? gap
           : (corner === "tc" || corner === "bc") ? Math.round((parent.width - width) / 2)
           : parent.width - width - gap
        y: (corner === "tc" || corner === "tr") ? gap
           : corner === "rc" ? Math.round((parent.height - height) / 2)
           : parent.height - height - gap
    }

    // ── Meldungen und Banner: oben mittig ───────────────────────────────────
    RaceMessage {
        anchors.horizontalCenter: parent.horizontalCenter
        baseY: 22
        z: 46
    }

    FastestLapBanner {
        anchors.horizontalCenter: parent.horizontalCenter
        baseY: 84
        z: 47
    }

    StartLights {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.height * 0.12
        z: 80
    }

    // ── Unten mittig: Battle-Boxen im Rennen, Hotlap-Boxen in der Quali ─────
    // Beide sitzen an derselben Stelle; sie schliessen sich gegenseitig aus
    // (Battles nur im Rennen, Hotlaps nur in der Quali).
    Battles {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                  bottomMargin: 28 }
        z: 40
    }

    Hotlaps {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                  bottomMargin: 28 }
        z: 40
    }

    LowerThird {
        x: (root.width - width) / 2
        baseY: root.height - 380 - height
        z: 41
    }

    DangerZone {
        anchors.horizontalCenter: parent.horizontalCenter
        baseY: root.height - 40 - height
        z: 46
    }

    // ── Unten links: Onboard, darueber die Pit-Projektion ───────────────────
    Onboard {
        id: onboard
        anchors { left: parent.left; bottom: parent.bottom
                  leftMargin: 24; bottomMargin: 28 }
        z: 44
    }

    PitProjection {
        anchors { left: parent.left; bottom: parent.bottom; leftMargin: 24 }
        // Im Web liegen beide auf 24 px ueber dem unteren Rand und ueberlappen
        // sich dort. Hier weicht die Projektion nach oben aus, wenn das Onboard
        // steht - sichtbar sind sie ohnehin selten gleichzeitig.
        anchors.bottomMargin: onboard.visible ? 28 + onboard.height + 10 : 24
        z: 44
    }

    // ── Unten rechts: Boxenstopps, darunter der WM-Stand ────────────────────
    PitCards {
        anchors { right: parent.right; bottom: parent.bottom
                  rightMargin: 24; bottomMargin: 28 }
        z: 45
    }

    Championship {
        anchors { right: parent.right; bottom: parent.bottom
                  rightMargin: 24; bottomMargin: 40 }
        z: 45
    }

    // ── Chart: legt sich mit abgedunkeltem Grund ueber alles ────────────────
    Charts {
        anchors.fill: parent
        z: 88
    }

    // Der Bearbeiten-Rahmen liegt ueber allem, ist aber nur sichtbar, wenn das
    // HUD entsperrt ist. Gesperrt gehen ohnehin alle Klicks durchs Fenster hindurch.
    EditFrame {
        anchors.fill: parent
        visible: !Hud.locked
        z: 9999
    }
}
