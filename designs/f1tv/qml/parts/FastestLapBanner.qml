// Schnellste Runde im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   [ FASTEST LAP ][ HAMILTON ][ 1:31.529 ]
//
// Lila Block, Nachname, Zeit. Ersetzt parts/FastestLapBanner.qml - gleiche Daten
// (Kers.fastestLap) und Schnittstelle (baseY, Ein-/Ausblenden von oben).

import QtQuick
import ".."
import "../Fmt.js" as Fmt
import "F1.js" as F1

Item {
    id: flb

    readonly property var src: Kers.fastestLap
    readonly property var ex: Theme.extra
    property real baseY: 0

    width: leiste.width
    height: leiste.height

    Row {
        id: leiste
        height: 40

        Rectangle {
            width: label.implicitWidth + 28
            height: parent.height
            topLeftRadius: Theme.panelRadius
            bottomLeftRadius: Theme.panelRadius
            color: flb.ex.purple || "#8f2ad8"
            Text {
                id: label
                anchors.centerIn: parent
                text: "FASTEST LAP"
                color: "#ffffff"
                font { family: Theme.display; pixelSize: 16; weight: Font.Black; letterSpacing: 1 }
            }
        }
        Rectangle {
            width: Math.max(130, nameText.implicitWidth + 30)
            height: parent.height
            color: Theme.panelBg
            Text {
                id: nameText
                anchors.centerIn: parent
                text: F1.nachname(flb.src.name)
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 18; weight: Font.Bold; letterSpacing: 0.5
                       capitalization: Font.AllUppercase }
            }
        }
        Rectangle {
            width: zeit.implicitWidth + 28
            height: parent.height
            topRightRadius: Theme.panelRadius
            bottomRightRadius: Theme.panelRadius
            color: flb.ex.lapBar || Theme.headerRowBg
            Text {
                id: zeit
                anchors.centerIn: parent
                text: Fmt.time(flb.src.lapTime)
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 20; weight: Font.Bold; features: ({ "tnum": 1 }) }
            }
        }
    }

    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : -16)
    Behavior on opacity { NumberAnimation { duration: 350 } }
    Behavior on y {
        NumberAnimation {
            duration: 450
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
        }
    }
}
