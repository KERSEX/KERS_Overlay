// Gefahrenzone-Countdown im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   [ DROP ZONE ][ 16 │ LAWSON ][ 2:55 ]
//
// Wer auf dem letzten sicheren Platz steht und wie lange die Session noch
// laeuft - schmal statt des grossen Kastens, damit es ueber den Hotlap-Karten
// Platz hat (Overlay.qml schiebt es dorthin). Ersetzt parts/DangerZone.qml,
// gleiche Daten (Kers.danger) und gleiche Schnittstelle (baseY, Ein-/Ausblenden).

import QtQuick
import ".."
import "../Fmt.js" as Fmt

Item {
    id: dz

    readonly property var src: Kers.danger
    readonly property var ex: Theme.extra
    property real baseY: 0

    width: leiste.width
    height: leiste.height

    Row {
        id: leiste
        height: 36

        Rectangle {
            width: label.implicitWidth + 24
            height: parent.height
            topLeftRadius: Theme.panelRadius
            bottomLeftRadius: Theme.panelRadius
            color: dz.ex.redFlag || "#e10600"
            Text {
                id: label
                anchors.centerIn: parent
                text: "DROP ZONE"
                color: "#ffffff"
                font { family: Theme.display; pixelSize: 15; weight: Font.Black; letterSpacing: 1 }
            }
            // Der rote Teil atmet, solange die Anzeige steht.
            SequentialAnimation on opacity {
                running: dz.src.isVisible
                loops: Animation.Infinite
                NumberAnimation { to: 0.7; duration: 700; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
            }
        }
        Rectangle {
            width: 34
            height: parent.height
            color: dz.ex.posBg || "#14151a"
            Text {
                anchors.centerIn: parent
                text: dz.src.cut
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 16; weight: Font.Bold; features: ({ "tnum": 1 }) }
            }
        }
        Rectangle {
            width: Math.max(120, nameText.implicitWidth + 28)
            height: parent.height
            color: Theme.panelBg
            Text {
                id: nameText
                anchors.centerIn: parent
                text: dz.src.name
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 16; weight: Font.Bold; letterSpacing: 0.5
                       capitalization: Font.AllUppercase }
            }
        }
        Rectangle {
            width: zeit.implicitWidth + 24
            height: parent.height
            topRightRadius: Theme.panelRadius
            bottomRightRadius: Theme.panelRadius
            color: dz.ex.lapBar || Theme.headerRowBg
            Text {
                id: zeit
                anchors.centerIn: parent
                text: Fmt.clock(dz.src.timeLeft)
                color: dz.ex.scYellow || "#ffd200"
                font { family: Theme.display; pixelSize: 18; weight: Font.Black; features: ({ "tnum": 1 }) }
            }
        }
    }

    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : 14)
    Behavior on opacity { NumberAnimation { duration: 400 } }
    Behavior on y { NumberAnimation { duration: 400 } }
}
