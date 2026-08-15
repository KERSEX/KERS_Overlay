// Die Battle-Boxen unten in der Mitte.
// Vorlage: #battle-area in static/parts/battles.css, Ablauf in battles.js.
// Die Gruppen-Erkennung samt Hysterese und Sticky-Zuordnung steckt in hud/parts.py.

import QtQuick
import ".."

// Grid statt Row, damit sich die Richtung umschalten laesst: vier Spalten
// verhalten sich wie eine Reihe, eine Spalte stapelt die Boxen uebereinander.
// Ein zweiter Positionierer daneben waere die Alternative gewesen - dann hinge
// das Repeater-Modell aber an zwei Stellen.
Grid {
    id: area

    columns: Kers.settings.battleDir === "column" ? 1 : 4
    spacing: 16
    // align-items: flex-end - unterschiedlich hohe Boxen stehen auf einer Linie.
    // ⚠ Ersetzt den frueheren `anchors.bottom` an den Boxen: ein Grid setzt x UND
    // y seiner Kinder, ein Anker dorthin wuerde sich damit beissen. Row liess y
    // frei, deshalb ging es vorher.
    verticalItemAlignment: Grid.AlignBottom

    Repeater {
        model: Kers.battles.boxes

        Item {
            id: slot
            required property var modelData
            readonly property var box: modelData

            width: visible ? 310 : 0
            height: card.height
            visible: box.isVisible || card.opacity > 0

            Rectangle {
                id: card
                width: 310
                height: header.height + sub.height + rows.height
                radius: Theme.panelRadius
                color: Theme.panelBg
                border.width: 1
                border.color: Theme.panelBorder
                clip: true

                // box-in / box-out: hochgleiten und aufblenden bzw. andersherum.
                opacity: slot.box.isVisible ? 1 : 0
                y: slot.box.isVisible ? 0 : 20
                scale: slot.box.isVisible ? 1 : 0.95
                Behavior on opacity { NumberAnimation { duration: 700 } }
                Behavior on y {
                    NumberAnimation { duration: 700; easing.type: Easing.Bezier
                                      easing.bezierCurve: [0.2, 0.85, 0.3, 1, 1, 1] }
                }
                Behavior on scale {
                    NumberAnimation { duration: 700; easing.type: Easing.Bezier
                                      easing.bezierCurve: [0.2, 0.85, 0.3, 1, 1, 1] }
                }

                // ── Kopf ────────────────────────────────────────────────────
                Rectangle {
                    id: header
                    width: parent.width
                    height: 32
                    // ⚠ Die Ecken muessen HIER noch einmal gesetzt werden. Die Karte
                    // ist zwar abgerundet, aber `clip: true` beschneidet in Qt Quick
                    // nur auf das Rechteck des Items - die Rundung ignoriert es. Ohne
                    // die beiden Radien deckt dieser Verlauf die oberen Ecken zu und
                    // die Box wirkt oben eckig. Unten faellt es nicht auf: die Zeilen
                    // sind dort nur 5 % Weiss und lassen die Rundung durchscheinen.
                    topLeftRadius: card.radius
                    topRightRadius: card.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.headBg1 }
                        GradientStop { position: 1.0; color: Theme.headBg2 }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: slot.box.header
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 19; weight: Font.Bold
                               letterSpacing: 4; capitalization: Font.AllUppercase }
                    }

                    // Der rote Strich unter dem Kopf laeuft nach aussen aus.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right
                                  bottom: parent.bottom }
                        height: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.00; color: "transparent" }
                            GradientStop { position: 0.35; color: Theme.accent }
                            GradientStop { position: 0.50; color: "#ff5a3c" }
                            GradientStop { position: 0.65; color: Theme.accent }
                            GradientStop { position: 1.00; color: "transparent" }
                        }
                    }
                }

                // ── "Kampf in X Runden" ─────────────────────────────────────
                Rectangle {
                    id: sub
                    anchors.top: header.bottom
                    width: parent.width
                    height: slot.box.subVisible ? 24 : 0
                    visible: height > 0
                    color: Qt.rgba(225 / 255, 6 / 255, 0, 0.12)

                    Rectangle {
                        anchors { left: parent.left; right: parent.right
                                  bottom: parent.bottom }
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 7

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⚔"
                            font.pixelSize: 15
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Kampf in"
                            color: "#ffb38a"
                            font { family: Theme.display; pixelSize: 14; letterSpacing: 1.5
                                   capitalization: Font.AllUppercase }
                        }
                        Text {
                            id: lapsText
                            anchors.verticalCenter: parent.verticalCenter
                            text: slot.box.subLaps
                            // Letzte Runde vor dem Kampf -> rot statt gelb.
                            color: slot.box.subLaps <= 1 ? "#ff5a5a" : "#ffd000"
                            font { family: Theme.display; pixelSize: 21; weight: Font.Bold }

                            // bs-pop: bei jeder Aenderung kurz aufploppen.
                            onTextChanged: popAnim.restart()
                            NumberAnimation {
                                id: popAnim
                                target: lapsText
                                property: "scale"
                                from: 1.5
                                to: 1.0
                                duration: 400
                                easing.type: Easing.OutQuad
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: slot.box.subLaps === 1 ? "Runde" : "Runden"
                            color: "#ffb38a"
                            font { family: Theme.display; pixelSize: 14; letterSpacing: 1
                                   capitalization: Font.AllUppercase }
                        }
                    }
                }

                // ── Die Fahrerzeilen ────────────────────────────────────────
                Item {
                    id: rows
                    anchors.top: sub.bottom
                    width: parent.width
                    height: rows.lastSlot >= 0 ? (rows.lastSlot + 1) * 44 : 0

                    // ⚠ HIER ausrechnen, nicht unten im Delegate. BattleRow hat eine
                    // eigene `required property int slot` - in seinem Scope verdeckt
                    // die den Namen der aeusseren id `slot`, und `slot.box...` liefe
                    // dort ins Leere. Genau daran ist die Rundung der letzten Zeile
                    // beim ersten Anlauf gescheitert. `rows` kollidiert mit nichts.
                    readonly property int lastSlot: slot.box.rows.count - 1

                    Repeater {
                        model: slot.box.rows
                        delegate: BattleRow {
                            width: rows.width
                            lastSlot: rows.lastSlot
                        }
                    }
                }
            }
        }
    }
}
