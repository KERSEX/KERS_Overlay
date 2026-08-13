// Gefahrenzone-Countdown (Quali): wer steht auf dem letzten sicheren Platz?
// Vorlage: #danger-zone in static/parts/danger.css, Ablauf in danger.js.

import QtQuick
import ".."
import "../Fmt.js" as Fmt

Item {
    id: dz

    readonly property var src: Kers.danger
    property real baseY: 0

    width: box.width
    height: box.height

    Rectangle {
        id: box
        width: Math.max(240, col.implicitWidth + 52)     // min-width 240, padding 26
        height: col.implicitHeight + 24
        radius: Theme.panelRadius
        color: Theme.panelBg
        border.width: 1
        border.color: Qt.rgba(225 / 255, 6 / 255, 0, 0.45)

        // dz-pulse: der Rand atmet, solange die Anzeige steht.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(225 / 255, 6 / 255, 0, 0.4)
            SequentialAnimation on opacity {
                running: dz.src.isVisible
                loops: Animation.Infinite
                NumberAnimation { to: 0.0; duration: 700 }
                NumberAnimation { to: 1.0; duration: 700 }
            }
        }

        Column {
            id: col
            anchors.centerIn: parent
            spacing: 0

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Gefahrenzone"
                color: "#9aa0aa"
                font { family: Theme.sans; pixelSize: 11; letterSpacing: 1
                       capitalization: Font.AllUppercase }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "P" + dz.src.cut
                color: "#ff5a5a"
                font { family: Theme.display; pixelSize: 40; weight: Font.Bold }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 2
                text: dz.src.name
                color: Theme.textMain
                font { family: Theme.sans; pixelSize: 13; weight: Font.DemiBold
                       capitalization: Font.AllUppercase }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 6
                text: Fmt.clock(dz.src.timeLeft)
                color: "#ffd000"
                font { family: Theme.display; pixelSize: 30
                       features: ({ "tnum": 1 }) }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "für eine gültige Runde"
                color: "#9aa0aa"
                font { family: Theme.sans; pixelSize: 11 }
            }
        }
    }

    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : 14)
    Behavior on opacity { NumberAnimation { duration: 400 } }
    Behavior on y { NumberAnimation { duration: 400 } }
}
