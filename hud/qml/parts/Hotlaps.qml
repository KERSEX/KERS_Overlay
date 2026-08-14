// Quali: die Hotlap-Boxen (wer faehrt gerade eine fliegende Runde?).
// Vorlage: #hotlap-area in static/parts/hotlap.css, Ablauf in hotlap.js.
//
// Sortiert nach Streckenfortschritt - wer als Naechstes ueber die Linie kommt,
// steht links. Die Auswahl trifft hud/parts.py.

import QtQuick
import ".."
import "../Fmt.js" as Fmt

Row {
    id: area

    spacing: 16

    // Die grosse Zeit zaehlt zwischen den Datenupdates fluessig weiter. Im Web macht
    // das eine rAF-Dauerschleife (tickHotlapTimes) - die war laut hud/README.md
    // mitverantwortlich fuer das Speicherwachstum. Hier laeuft ein Timer, und zwar
    // nur solange ueberhaupt eine Box steht.
    property real clock: 0
    Timer {
        running: Kers.hotlaps.boxes.count > 0
        repeat: true
        interval: 33
        onTriggered: area.clock = Date.now()
    }

    Repeater {
        model: Kers.hotlaps.boxes

        Rectangle {
            id: card

            required property int slot
            required property int position
            required property string name
            required property color teamColor
            required property string teamLogo
            required property string tyreIcon
            required property int tyreStamp
            required property bool invalid
            required property real timeBase
            required property real timeAt
            required property bool ticking
            required property real staticTime
            required property real delta
            required property bool deltaUp
            required property bool hasDelta
            required property var sectors
            required property var sectorClasses

            width: 300
            height: head.height + body.implicitHeight
            radius: Theme.panelRadius
            color: Theme.panelBg
            border.width: 1
            border.color: Theme.panelBorder
            clip: true
            anchors.bottom: parent ? parent.bottom : undefined

            // box-in
            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: 600 } }

            // border-top in Teamfarbe - rundet oben mit, siehe AccentTop.qml
            AccentTop {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                color: card.teamColor
                radius: card.radius
                z: 2
            }

            // ── Kopf ────────────────────────────────────────────────────────
            Rectangle {
                id: head
                width: parent.width
                height: 30
                // ⚠ Ecken hier noch einmal setzen: `clip: true` auf der Karte
                // beschneidet in Qt Quick nur rechteckig, nie auf die Rundung -
                // sonst deckt dieser Verlauf die oberen Ecken zu.
                topLeftRadius: card.radius
                topRightRadius: card.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.headBg1 }
                    GradientStop { position: 1.0; color: Theme.headBg2 }
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.06)
                }

                Row {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 8

                    // Der Punkt blinkt gruen - rot, wenn die Runde ungueltig ist.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9
                        height: 9
                        radius: 4.5
                        color: card.invalid ? "#ff3b30" : "#00e676"
                        SequentialAnimation on opacity {
                            running: true
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.4; duration: 650 }
                            NumberAnimation { to: 1.0; duration: 650 }
                        }
                    }
                    // ⚠ Der Versatz ist gemessen, nicht geschaetzt. verticalCenter
                    // zentriert den TEXTKASTEN, und der haelt bei Teko unten Platz
                    // fuer Unterlaengen frei: bei 16 px sind es 15 px Oberlaenge,
                    // 8 px Unterlaenge, aber nur 10,3 px Versalhoehe. Weil hier nur
                    // Grossbuchstaben stehen, sitzt die Schrift dadurch sichtbar zu
                    // hoch - die Grundlinie muss 1,6 px tiefer (bei der P-Nummer
                    // 1,5 px). Gerundet auf 2 px, gleich fuer beide, damit sie
                    // untereinander bündig bleiben.
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 2
                        text: "Hot Lap"
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 16; weight: Font.Bold
                               letterSpacing: 3; capitalization: Font.AllUppercase }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 2
                        text: "P" + card.position
                        color: "#cfd0d8"
                        font { family: Theme.display; pixelSize: 17; weight: Font.DemiBold }
                    }
                }

                Image {
                    anchors { right: parent.right; rightMargin: 12
                              verticalCenter: parent.verticalCenter }
                    width: 24
                    height: 24
                    source: card.tyreIcon ? Theme.tyre(card.tyreIcon) : ""
                    visible: card.tyreIcon !== ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize { width: 48; height: 48 }
                    smooth: true
                }
            }

            // ── Koerper ─────────────────────────────────────────────────────
            Item {
                id: body
                anchors.top: head.bottom
                width: parent.width
                implicitHeight: bodyCol.implicitHeight + 20

                Rectangle {
                    anchors.fill: parent
                    opacity: 0.08
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.00; color: card.teamColor }
                        GradientStop { position: 0.42; color: "transparent" }
                    }
                }

                Column {
                    id: bodyCol
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              leftMargin: 13; rightMargin: 13; topMargin: 9 }
                    spacing: 8

                    Row {
                        spacing: 8
                        width: parent.width
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22
                            height: 22
                            source: card.teamLogo ? Theme.teamLogo(card.teamLogo) : ""
                            visible: card.teamLogo !== ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize { width: 44; height: 44 }
                            smooth: true
                            // Breite Logos (Aston Martin, Cadillac) schrumpfen stark -
                            // ohne Mipmaps werden sie koernig, siehe TowerRow.qml.
                            mipmap: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 30
                            text: card.name
                            color: Theme.textMain
                            elide: Text.ElideRight
                            font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold
                                   letterSpacing: 0.3; capitalization: Font.AllUppercase }
                        }
                    }

                    Row {
                        spacing: 10
                        Text {
                            anchors.baseline: parent.children[1].baseline
                            // Laufende Runde: Basiswert plus vergangene Zeit seit
                            // dem letzten Payload. Steht die Uhr, die letzte Runde.
                            text: card.ticking
                                  ? Fmt.time(card.timeBase + Math.max(0, area.clock - card.timeAt) / 1000)
                                  : (card.staticTime > 0 ? Fmt.time(card.staticTime) : "—")
                            color: card.invalid ? "#ff5a5a" : Theme.textMain
                            font { family: Theme.display; pixelSize: 32
                                   weight: Font.DemiBold; features: ({ "tnum": 1 }) }
                        }
                        Text {
                            text: card.hasDelta
                                  ? (card.deltaUp ? "−" : "+") + Math.abs(card.delta).toFixed(3)
                                  : ""
                            color: card.deltaUp ? "#00d56a" : "#ffd000"
                            font { family: Theme.display; pixelSize: 19
                                   weight: Font.DemiBold; features: ({ "tnum": 1 }) }
                        }
                    }

                    Row {
                        spacing: 7
                        width: parent.width

                        Repeater {
                            model: 3
                            Rectangle {
                                required property int index
                                width: (parent.width - 14) / 3
                                height: 34
                                radius: 5
                                color: Qt.rgba(1, 1, 1, 0.05)

                                Column {
                                    anchors.centerIn: parent
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "S" + (index + 1)
                                        color: Theme.textMuted
                                        font { family: Theme.sans; pixelSize: 9
                                               weight: Font.Bold; letterSpacing: 1 }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: Fmt.time(card.sectors[index]) || "—"
                                        color: card.sectorClasses[index]
                                               ? Fmt.sectorColor(card.sectorClasses[index], false)
                                               : "#cccdd6"
                                        font { family: Theme.display; pixelSize: 16
                                               weight: Font.DemiBold
                                               features: ({ "tnum": 1 }) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
