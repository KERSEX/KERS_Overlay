// WM-Stand aus championship.json plus Live-Hochrechnung.
// Vorlage: #champ-panel in static/parts/champ.css, Ablauf in champ.js.
//
// Ein MANUELLES Panel: es erscheint nur, wenn es in /regie eingeschaltet ist.

import QtQuick
import ".."

Rectangle {
    id: champ

    readonly property var src: Kers.championship

    width: 420
    height: col.implicitHeight + 8                       // padding-bottom: 8px
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

        Item {
            width: parent.width
            height: 34
            Text {
                anchors { left: parent.left; leftMargin: 16
                          verticalCenter: parent.verticalCenter }
                text: champ.src.title + (champ.src.live ? " · wenn heute so endet" : "")
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 17; letterSpacing: 2
                       capitalization: Font.AllUppercase }
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.accentWidth
                color: Theme.accent
            }
        }

        Repeater {
            model: champ.src.rows
            Item {
                required property var modelData
                width: col.width
                height: 32

                // Teamfarbe als Kante links plus auslaufender Schleier
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: Theme.accentWidth
                    color: modelData.color
                }
                Rectangle {
                    anchors.fill: parent
                    opacity: 0.10
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.00; color: modelData.color }
                        GradientStop { position: 0.45; color: "transparent" }
                    }
                }

                Row {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 10

                    Text {
                        width: 22
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.pos
                        color: Theme.textMuted
                        font { family: Theme.display; pixelSize: 16 }
                    }
                    Text {
                        width: parent.width - 22 - 42 - 36 - 30
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.name
                        color: Theme.textMain
                        elide: Text.ElideRight
                        font { family: Theme.sans; pixelSize: 13; weight: Font.DemiBold
                               capitalization: Font.AllUppercase }
                    }
                    Text {
                        width: 42
                        height: parent.height
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.points
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 17; weight: Font.DemiBold }
                    }
                    Text {
                        width: 36
                        height: parent.height
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.livePoints > 0 ? "+" + modelData.livePoints : ""
                        color: "#00e676"
                        font { family: Theme.sans; pixelSize: 11 }
                    }
                }
            }
        }
    }
}
