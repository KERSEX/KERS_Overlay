// Farbige Kante an der Oberkante einer Panelkarte, die deren Rundung mitmacht.
//
// ⚠ Warum das kein schlichtes Rectangle mit topLeftRadius sein kann: Qt kappt
// jeden Eckradius auf die HAELFTE der kleineren Kantenlaenge. Bei 3 px Hoehe
// bleiben von 10 px also 1,5 px uebrig - der Balken stuende fast eckig vor der
// runden Ecke, und genau so sah es an Onboard und Hotlap-Boxen aus.
//
// Deshalb ein hohes Rechteck mit der richtigen Rundung, das ein flacher
// clip-Container auf die Akzenthoehe beschneidet. Geschnitten wird UNTEN, also
// an einer geraden Kante - dass `clip: true` in Qt Quick rechteckig beschneidet
// und die Rundung selbst nicht anfasst, faellt dort nicht ins Gewicht.
//
// Anwendung (die Karte muss breiter als 2x radius sein, sonst kappt Qt erneut):
//
//     AccentTop {
//         anchors { left: parent.left; right: parent.right; top: parent.top }
//         color: karte.teamColor
//         radius: karte.radius
//         z: 2
//     }

import QtQuick
import ".."

Item {
    id: kante

    property color color: "white"
    property real radius: Theme.panelRadius

    height: Theme.accentWidth
    clip: true

    Rectangle {
        width: parent.width
        // Doppelt so hoch wie der Radius: erst dann traegt das Rechteck ihn
        // ungekappt. Sichtbar bleibt davon nur der oberste Streifen.
        height: kante.radius * 2
        color: kante.color
        topLeftRadius: kante.radius
        topRightRadius: kante.radius
    }
}
