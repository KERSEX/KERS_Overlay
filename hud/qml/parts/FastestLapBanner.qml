// Das lila Bestrunden-Banner.
// Vorlage: #fl-banner in static/parts/flbanner.css, Ablauf in flbanner.js.

import QtQuick
import ".."
import "../Fmt.js" as Fmt

Item {
    id: flb

    readonly property var src: Kers.fastestLap
    property real baseY: 0

    width: box.width
    height: box.height

    Rectangle {
        id: box
        width: row.implicitWidth + 48                    // padding: 10px 24px
        height: row.implicitHeight + 20
        radius: Theme.panelRadius
        border.width: 1
        border.color: Qt.rgba(177 / 255, 75 / 255, 1, 0.55)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(177 / 255, 75 / 255, 1, 0.30) }
            GradientStop { position: 1.0; color: Theme.panelBg }
        }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 14

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "⚡"
                font.pixelSize: 24
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: "FASTEST LAP"
                    color: "#d49bff"
                    font { family: Theme.display; pixelSize: 15; weight: Font.Bold
                           letterSpacing: 3 }
                }
                Text {
                    text: flb.src.name
                    color: Theme.textMain
                    font { family: Theme.sans; pixelSize: 18; weight: Font.Bold
                           capitalization: Font.AllUppercase }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Fmt.time(flb.src.lapTime)
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 30; weight: Font.DemiBold
                       features: ({ "tnum": 1 }) }
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
