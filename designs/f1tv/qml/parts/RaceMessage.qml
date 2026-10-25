// Meldungen im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   ▌[ RACE CONTROL ][ 5 SEKUNDEN STRAFE · RUSSELL ]
//
// Streifen in der Farbe der Meldung (Strafe rot, Track Limits gelb, Safety Car
// gelb, freigegeben gruen ...), daneben die Art - "RACE CONTROL" fuer Rennleitung
// und Safety Car, "STRATEGY" beim Undercut-Alarm - und der Text. Ersetzt
// parts/RaceMessage.qml: gleiche Daten (Kers.banner) und Schnittstelle (baseY,
// Ein- und Ausblenden von oben).

import QtQuick
import ".."

Item {
    id: banner

    readonly property var src: Kers.banner
    readonly property var ex: Theme.extra
    property real baseY: 0

    width: leiste.width
    height: leiste.height

    Row {
        id: leiste
        height: 40

        Rectangle {                              // Farbe der Meldung
            width: 8
            height: parent.height
            topLeftRadius: Theme.panelRadius
            bottomLeftRadius: Theme.panelRadius
            color: banner.src.accent
        }
        Rectangle {
            width: art.implicitWidth + 26
            height: parent.height
            color: banner.ex.lapBar || Theme.headerRowBg
            Text {
                id: art
                anchors.centerIn: parent
                text: banner.src.kind === "strategy" ? "STRATEGY" : "RACE CONTROL"
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 13; weight: Font.Black; letterSpacing: 1.2 }
            }
        }
        Rectangle {
            width: Math.max(220, meldung.implicitWidth + 34)
            height: parent.height
            topRightRadius: Theme.panelRadius
            bottomRightRadius: Theme.panelRadius
            color: Theme.panelBg
            Text {
                id: meldung
                anchors.centerIn: parent
                text: banner.src.text
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 17; weight: Font.Bold; letterSpacing: 0.4
                       capitalization: Font.AllUppercase }
            }
        }
    }

    // Einblenden: gleichzeitig aufblenden und 18 px hochgleiten - wie das Original.
    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : -18)
    Behavior on opacity { NumberAnimation { duration: 350 } }
    Behavior on y {
        NumberAnimation {
            duration: 450
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
        }
    }
}
