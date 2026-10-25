// Battle-Boxen im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   ┌──────────────────────────┐
//   │      BATTLE FOR P4       │
//   │ 4  [Logo] PIASTRI        S │
//   │ 5  [Logo] LECLERC +0.412 M │
//   └──────────────────────────┘
//
// Wie ein kleiner Tower: Positionsspalte, Teamlogo, voller Name, Abstand zum
// Vordermann, Reifenbuchstabe. Ersetzt parts/Battles.qml, gleiche Daten
// (Kers.battles.boxes) und dieselbe Anordnung (Reihe oder Spalte aus /settings).

import QtQuick
import ".."

Grid {
    id: area

    readonly property var ex: Theme.extra
    readonly property int rowH: 34

    columns: Kers.settings.battleDir === "column" ? 1 : 4
    spacing: 12
    verticalItemAlignment: Grid.AlignBottom

    Repeater {
        model: Kers.battles.boxes

        Item {
            id: slot
            required property var modelData
            readonly property var box: modelData

            width: visible ? 300 : 0
            height: card.height
            visible: box.isVisible || card.opacity > 0

            Rectangle {
                id: card
                width: 300
                height: header.height + sub.height + rows.height
                radius: Theme.panelRadius
                color: Theme.panelBg
                border.width: 1
                border.color: Theme.panelBorder

                opacity: slot.box.isVisible ? 1 : 0
                y: slot.box.isVisible ? 0 : 16
                Behavior on opacity { NumberAnimation { duration: 500 } }
                Behavior on y { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }

                // ── Kopf ────────────────────────────────────────────────────
                Rectangle {
                    id: header
                    width: parent.width
                    height: 30
                    topLeftRadius: card.radius
                    topRightRadius: card.radius
                    color: area.ex.lapBar || Theme.headerRowBg
                    Text {
                        anchors.centerIn: parent
                        text: slot.box.header
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 15; weight: Font.Bold; letterSpacing: 1
                               capitalization: Font.AllUppercase }
                    }
                }

                // ── "Battle in X Laps" (Ueberhol-Vorhersage) ────────────────
                Rectangle {
                    id: sub
                    anchors.top: header.bottom
                    width: parent.width
                    height: slot.box.subVisible ? 24 : 0
                    visible: height > 0
                    color: Qt.rgba(1, 1, 1, 0.05)
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.baseline: laps.baseline
                            text: "BATTLE IN"
                            color: Theme.textMuted
                            font { family: Theme.display; pixelSize: 12; weight: Font.DemiBold; letterSpacing: 1 }
                        }
                        Text {
                            id: laps
                            text: slot.box.subLaps
                            color: slot.box.subLaps <= 1 ? (area.ex.loss || "#e10600") : (area.ex.scYellow || "#ffd200")
                            font { family: Theme.display; pixelSize: 16; weight: Font.Black; features: ({ "tnum": 1 }) }
                        }
                        Text {
                            anchors.baseline: laps.baseline
                            text: slot.box.subLaps === 1 ? "LAP" : "LAPS"
                            color: Theme.textMuted
                            font { family: Theme.display; pixelSize: 12; weight: Font.DemiBold; letterSpacing: 1 }
                        }
                    }
                }

                // ── Die Fahrerzeilen ────────────────────────────────────────
                Item {
                    id: rows
                    anchors.top: sub.bottom
                    width: parent.width
                    height: rows.lastSlot >= 0 ? (rows.lastSlot + 1) * area.rowH : 0
                    // ⚠ Hier ausrechnen, nicht im Delegate - F1BattleRow hat eine
                    // eigene `slot`-Rolle, die die aeussere id verdeckt (siehe
                    // parts/Battles.qml).
                    readonly property int lastSlot: slot.box.rows.count - 1

                    Repeater {
                        model: slot.box.rows
                        delegate: F1BattleRow {
                            width: rows.width
                            rowH: area.rowH
                            lastSlot: rows.lastSlot
                        }
                    }
                }
            }
        }
    }
}
