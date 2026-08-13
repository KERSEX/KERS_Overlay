// Live-Boxenstopp-Timer: bis zu drei Karten uebereinander.
// Vorlage: #pit-stack in static/parts/pit.css, Ablauf in pit.js.

import QtQuick
import ".."

Column {
    id: stack

    spacing: 10

    // Die laufende Zeit zaehlt in QML weiter - Python liefert nur den Startzeitpunkt.
    // Der Timer laeuft nur, solange ueberhaupt eine Karte steht; im Web war das eine
    // rAF-Dauerschleife, und genau die hat laut hud/README.md den Speicher wachsen
    // lassen. Fertige Karten verschwinden nach 5 s, laenger laeuft er also nie nach.
    property real clock: Date.now()
    Timer {
        running: Kers.pit.cards.count > 0
        repeat: true
        interval: 66
        onTriggered: stack.clock = Date.now()
    }

    Repeater {
        model: Kers.pit.cards

        // ⚠ Diese Huelle ist Absicht und darf nicht wegoptimiert werden.
        // Die Einblend-Animation verschiebt die Karte um 20 px nach oben. Laege
        // die Animation auf dem DIREKTEN Kind des Column, wuerde sie dem
        // Positionierer die y-Koordinate wegnehmen - und dann landen alle Karten
        // uebereinander auf y = 0 (genau der Fehler beim ersten Versuch: hinter
        // "HADJAR" schaute noch "ANTONELLI" hervor). Der Column positioniert also
        // die Huelle, animiert wird die Karte darin.
        Item {
            id: holder

            required property int cardId
            required property string name
            required property color teamColor
            required property bool live
            required property real t0
            required property real finalTime
            required property string oldTyre
            required property string newTyre
            required property bool showArrow
            required property bool newWing
            required property int exitPos

            width: 280
            height: card.height

            Rectangle {
                id: card
                width: parent.width
                height: head.height + bodyRow.height + (exit.visible ? exit.height : 0)
                radius: Theme.panelRadius
                color: Theme.panelBg
                border.width: 1
                border.color: Theme.panelBorder
                clip: true

                // box-in: hochgleiten und aufblenden
                opacity: 0
                y: 20
                NumberAnimation on opacity {
                    to: 1; duration: 450
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
                }
                NumberAnimation on y {
                    to: 0; duration: 450
                    easing.type: Easing.Bezier
                    easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
                }

                // ── Kopf ────────────────────────────────────────────────────
                Rectangle {
                    id: head
                    width: parent.width
                    height: 26
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.headBg1 }
                        GradientStop { position: 1.0; color: Theme.headBg2 }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: holder.live ? "IN DER BOX" : "STANDZEIT"
                        color: holder.live ? "#ffcf3a" : Theme.textMain
                        font { family: Theme.display; pixelSize: 16; weight: Font.Bold
                               letterSpacing: 3 }
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right
                                  bottom: parent.bottom }
                        height: 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 0.5
                                           color: holder.live ? "#ffcf3a" : Theme.accent }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                }

                // ── Fahrer, Reifen, Zeit ────────────────────────────────────
                Item {
                    id: bodyRow
                    anchors.top: head.bottom
                    width: parent.width
                    height: 50

                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: Theme.accentWidth
                        color: holder.teamColor
                    }
                    Rectangle {
                        anchors.fill: parent
                        opacity: 0.10
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.00; color: holder.teamColor }
                            GradientStop { position: 0.45; color: "transparent" }
                        }
                    }

                    // Kein Row fuer die ganze Zeile: die Reifenspalte ist mal ein
                    // Bild, mal zwei mit Pfeil, mal noch ein Fluegelsymbol dazu.
                    // Mit Ankern ergibt sich die Restbreite fuer den Namen von selbst.
                    Text {
                        anchors {
                            left: parent.left; leftMargin: 14
                            right: tyres.left; rightMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                        text: holder.name
                        color: Theme.textMain
                        elide: Text.ElideRight
                        font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold
                               capitalization: Font.AllUppercase }
                    }

                    Row {
                        id: tyres
                        anchors {
                            right: timeText.left; rightMargin: 10
                            verticalCenter: parent.verticalCenter
                        }
                        height: parent.height
                        spacing: 6

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: holder.oldTyre !== ""
                            width: visible ? 28 : 0
                            height: 28
                            source: holder.oldTyre ? Theme.tyre(holder.oldTyre) : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize { width: 56; height: 56 }
                            smooth: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: holder.showArrow
                            text: "→"
                            color: "#6a6a78"
                        }
                        Image {
                            id: newTyreImg
                            anchors.verticalCenter: parent.verticalCenter
                            visible: holder.newTyre !== ""
                            width: visible ? 28 : 0
                            height: 28
                            source: holder.newTyre ? Theme.tyre(holder.newTyre) : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize { width: 56; height: 56 }
                            smooth: true
                            transformOrigin: Item.Center

                            // Der frische Reifen dreht sich herein, sobald die
                            // Karte auf "Standzeit" umspringt.
                            ParallelAnimation {
                                running: holder.showArrow
                                NumberAnimation {
                                    target: newTyreImg; property: "rotation"
                                    from: -200; to: 0; duration: 600
                                    easing.type: Easing.Bezier
                                    easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1]
                                }
                                NumberAnimation {
                                    target: newTyreImg; property: "scale"
                                    from: 0.3; to: 1; duration: 600
                                    easing.type: Easing.Bezier
                                    easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1]
                                }
                            }
                        }

                        // Neuer Frontfluegel montiert
                        MaskedIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: holder.newWing
                            width: visible ? 22 : 0
                            height: 20
                            rotation: 180
                            source: Theme.damage("FWR")
                            color: "#27e06a"
                        }
                    }

                    Text {
                        id: timeText
                        anchors {
                            right: parent.right; rightMargin: 14
                            verticalCenter: parent.verticalCenter
                        }
                        width: 52
                        horizontalAlignment: Text.AlignRight
                        text: holder.live
                              ? ((Math.max(0, stack.clock - holder.t0) / 1000).toFixed(1) + "s")
                              : (holder.finalTime > 0
                                 ? holder.finalTime.toFixed(1) + "s" : "—")
                        color: holder.live ? "#ffcf3a" : "#00e676"
                        font { family: Theme.display; pixelSize: 22; weight: Font.DemiBold
                               features: ({ "tnum": 1 }) }
                    }
                }

                // ── Auskommen-Position ──────────────────────────────────────
                Item {
                    id: exit
                    anchors.top: bodyRow.bottom
                    width: parent.width
                    height: 22
                    visible: holder.exitPos > 0

                    Row {
                        anchors { right: parent.right; rightMargin: 14
                                  verticalCenter: parent.verticalCenter }
                        spacing: 4
                        Text {
                            anchors.baseline: pos.baseline
                            text: "Box out window"
                            color: Theme.textMain
                            font { family: Theme.sans; pixelSize: 11 }
                        }
                        Text {
                            id: pos
                            text: "P" + holder.exitPos
                            color: Theme.textMain
                            font { family: Theme.display; pixelSize: 15 }
                        }
                    }
                }
            }
        }
    }
}
