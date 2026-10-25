// Boxenstopps im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   ┌──────────┬───────────────────────┬───────┐
//   │ PIT STOP │ [Logo] FINN CHEST      │  2.4s │
//   ├──────────┴───────────────────────┴───────┤  Teamfarbe
//   │ S → M   NEUER FRONTFLÜGEL         EXIT P7 │
//   └──────────────────────────────────────────┘
//
// Solange er steht: "PIT STOP" gelb und die Uhr laeuft. Danach die Standzeit,
// darunter Reifenwechsel, neuer Frontfluegel und wo er wieder rauskommt.
// Ersetzt parts/PitCards.qml - gleiche Daten (Kers.pit.cards), gleiche
// Anordnung (Stapel unten rechts).

import QtQuick
import ".."
import "F1.js" as F1

Column {
    id: stack

    spacing: 10
    readonly property var ex: Theme.extra

    // Laufende Standzeit zwischen den Datenupdates weiterzaehlen - nur solange Karten stehen.
    property real clock: Date.now()
    Timer {
        running: Kers.pit.cards.count > 0
        repeat: true
        interval: 66
        onTriggered: stack.clock = Date.now()
    }

    Repeater {
        model: Kers.pit.cards

        Item {
            id: holder

            required property int cardId
            required property string name
            required property color teamColor
            required property string teamLogo
            required property bool live
            required property real t0
            required property real finalTime
            required property string oldTyre
            required property string newTyre
            required property bool showArrow
            required property bool newWing
            required property int exitPos

            readonly property bool unten: holder.newTyre !== "" || holder.newWing || holder.exitPos > 0

            width: 320
            height: karte.height

            Item {
                id: karte
                width: parent.width
                height: oben.height + 2 + (holder.unten ? info.height : 0)

                opacity: 0
                y: 20
                NumberAnimation on opacity { to: 1; duration: 450; easing.type: Easing.OutCubic }
                NumberAnimation on y { to: 0; duration: 450; easing.type: Easing.OutCubic }

                Row {
                    id: oben
                    height: 36

                    Rectangle {
                        width: 92
                        height: parent.height
                        topLeftRadius: Theme.panelRadius
                        bottomLeftRadius: holder.unten ? 0 : Theme.panelRadius
                        color: holder.live ? (stack.ex.scYellow || "#ffd200") : (stack.ex.lapBar || Theme.headerRowBg)
                        Behavior on color { ColorAnimation { duration: 300 } }
                        Text {
                            anchors.centerIn: parent
                            text: "PIT STOP"
                            color: holder.live ? "#111111" : Theme.textMain
                            font { family: Theme.display; pixelSize: 14; weight: Font.Black; letterSpacing: 1 }
                        }
                    }
                    Rectangle {
                        width: karte.width - 92 - zeitBox.width
                        height: parent.height
                        color: Theme.panelBg
                        Image {
                            id: logo
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            source: holder.teamLogo ? Theme.teamLogo(holder.teamLogo) : ""
                            visible: holder.teamLogo !== ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize { width: 40; height: 40 }
                            smooth: true
                            mipmap: true
                        }
                        Text {
                            anchors { left: parent.left; leftMargin: logo.visible ? 38 : 12
                                      right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: holder.name
                            elide: Text.ElideRight
                            color: Theme.textMain
                            font { family: Theme.display; pixelSize: 15; weight: Font.Bold; letterSpacing: 0.3
                                   capitalization: Font.AllUppercase }
                        }
                    }
                    Rectangle {
                        id: zeitBox
                        width: 70
                        height: parent.height
                        topRightRadius: Theme.panelRadius
                        bottomRightRadius: holder.unten ? 0 : Theme.panelRadius
                        color: stack.ex.lapBar || Theme.headerRowBg
                        Text {
                            anchors.centerIn: parent
                            text: holder.live
                                  ? ((Math.max(0, stack.clock - holder.t0) / 1000).toFixed(1) + "s")
                                  : (holder.finalTime > 0 ? holder.finalTime.toFixed(1) + "s" : "–")
                            color: holder.live ? (stack.ex.scYellow || "#ffd200") : Theme.textMain
                            font { family: Theme.display; pixelSize: 19; weight: Font.Black; features: ({ "tnum": 1 }) }
                        }
                    }
                }
                Rectangle {                          // Teamfarbe unter der Kopfzeile
                    anchors.top: oben.bottom
                    width: parent.width
                    height: 2
                    color: holder.teamColor
                }

                // ── Reifen, Fluegel, Ausfahrt ───────────────────────────────
                Rectangle {
                    id: info
                    anchors.top: oben.bottom
                    anchors.topMargin: 2
                    width: parent.width
                    height: 26
                    visible: holder.unten
                    bottomLeftRadius: Theme.panelRadius
                    bottomRightRadius: Theme.panelRadius
                    color: Theme.panelBg

                    Row {
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        spacing: 6
                        Text {
                            visible: holder.oldTyre !== ""
                            text: holder.oldTyre
                            color: F1.reifenFarbe(holder.oldTyre, Theme.textMuted)
                            font { family: Theme.display; pixelSize: 14; weight: Font.Black }
                        }
                        Text {
                            visible: holder.showArrow
                            text: "→"
                            color: Theme.textMuted
                            font { family: Theme.display; pixelSize: 14 }
                        }
                        Text {
                            visible: holder.newTyre !== ""
                            text: holder.newTyre
                            color: F1.reifenFarbe(holder.newTyre, Theme.textMuted)
                            font { family: Theme.display; pixelSize: 14; weight: Font.Black }
                        }
                        Text {
                            visible: holder.newWing
                            leftPadding: 6
                            text: "NEUER FRONTFLÜGEL"
                            color: stack.ex.gain || "#2ad04b"
                            font { family: Theme.display; pixelSize: 12; weight: Font.Bold; letterSpacing: 0.5 }
                        }
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        visible: holder.exitPos > 0
                        text: "EXIT P" + holder.exitPos
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 13; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                }
            }
        }
    }
}
