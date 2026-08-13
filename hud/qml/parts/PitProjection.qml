// "Wo kaeme er nach einem Boxenstopp raus?" - nur bei beschaedigtem Kamera-Fahrer.
// Vorlage: #pit-proj in static/parts/pitproj.css, Ablauf in pitproj.js.

import QtQuick
import ".."

Rectangle {
    id: pp

    readonly property var src: Kers.pitProjection

    width: 230
    height: col.implicitHeight
    radius: Theme.panelRadius
    color: Theme.panelBg
    border.width: 1
    border.color: Theme.panelBorder
    clip: true

    opacity: src.isVisible ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 350 } }

    Column {
        id: col
        width: parent.width

        Text {
            leftPadding: 14
            topPadding: 7
            bottomPadding: 2
            width: parent.width
            elide: Text.ElideRight
            text: "Nach Boxenstopp · " + pp.src.name
            color: Theme.textMuted
            font { family: Theme.display; pixelSize: 13; letterSpacing: 1
                   capitalization: Font.AllUppercase }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            bottomPadding: 12
            topPadding: 2
            spacing: 14

            Column {
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "P" + pp.src.current
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 34; weight: Font.Bold }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Jetzt"
                    color: Theme.textMuted
                    font { family: Theme.sans; pixelSize: 9; letterSpacing: 1
                           capitalization: Font.AllUppercase }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "→"
                color: "#ffcc00"
                font.pixelSize: 20
            }

            Column {
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "P" + pp.src.projected
                    color: "#ff9a5a"
                    font { family: Theme.display; pixelSize: 34; weight: Font.Bold }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "danach"
                    color: Theme.textMuted
                    font { family: Theme.sans; pixelSize: 9; letterSpacing: 1
                           capitalization: Font.AllUppercase }
                }
            }
        }
    }
}
