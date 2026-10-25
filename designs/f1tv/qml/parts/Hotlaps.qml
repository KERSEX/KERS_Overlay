// Quali: Hotlap-Karten im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   [P] [Logo] VOLLER NAME                    S
//   ─────────────────── Teamfarbe ───────────────
//   1:31.3                          1:31.529
//                                   HAMILTON
//      S1          S2          S3
//   ▔▔▔▔▔▔▔    ▔▔▔▔▔▔▔    ▔▔▔▔▔▔▔   (gruen/gelb/lila je Sektor)
//
// Rechts steht das Ziel wie im Fernsehen: die Pole - oder, wer gerade
// ausscheiden wuerde, die Zeit an der Cut-Linie -, und zwar als Zwischenzeit am
// naechsten Sektor (rechnet die Bruecke, siehe parts_hotlaps.cpp). Nach der
// Linie bleibt die Karte 5 s stehen: gefahrene Zeit (lila = Bestzeit der
// Session, gruen = persoenliche), rechts das Delta aufs Ziel, vorn die neue
// Position. Die Karte des Kamera-Fahrers hat einen weissen Rahmen.
//
// Ersetzt parts/Hotlaps.qml. Gleiche Daten (Kers.hotlaps.boxes), gleiche Groessen-
// Schnittstelle zu Overlay.qml (ein Row, dessen Breite/Hoehe zaehlt).

import QtQuick
import ".."
import "../Fmt.js" as Fmt
import "F1.js" as F1

Row {
    id: area

    spacing: 10
    readonly property var ex: Theme.extra

    // Laufende Zeit zwischen den Datenupdates weiterzaehlen - nur solange Karten stehen.
    property real clock: 0
    Timer {
        running: Kers.hotlaps.boxes.count > 0
        repeat: true
        interval: 50
        onTriggered: area.clock = Date.now()
    }

    Repeater {
        model: Kers.hotlaps.boxes

        Item {
            id: card

            required property int slot
            required property int position
            required property string name
            required property color teamColor
            required property string teamLogo
            required property string tyreIcon
            required property bool invalid
            required property real timeBase
            required property real timeAt
            required property bool ticking
            required property real staticTime
            required property var sectors
            required property var sectorClasses
            required property bool focused
            required property bool finished
            required property string lapClass
            required property real finalDelta
            required property bool hasFinal
            required property real targetTime
            required property string targetName

            width: 330
            height: 114
            anchors.bottom: parent ? parent.bottom : undefined
            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: 400 } }

            /** Farbe der grossen Zeit: rot bei ungueltiger Runde, nach der Linie
             *  lila/gruen fuer Bestzeit der Session/persoenliche Bestzeit. */
            readonly property color zeitFarbe: card.invalid ? "#ff5a5a"
                                             : card.finished && card.lapClass !== ""
                                               ? Fmt.sectorColor(card.lapClass, false)
                                               : Theme.textMain

            Rectangle {
                anchors.fill: parent
                radius: Theme.panelRadius
                color: Theme.panelBg
                border.width: 1
                border.color: Theme.panelBorder
            }
            // Kamera-Fahrer: weisser Rahmen UEBER dem Inhalt (sonst verdecken
            // Positionskasten und Teamlinie ihn an den Kanten).
            Rectangle {
                anchors.fill: parent
                radius: Theme.panelRadius
                color: "transparent"
                border.width: 2
                border.color: area.ex.focus || "#ffffff"
                visible: card.focused
                z: 5
            }

            // ── Kopf ────────────────────────────────────────────────────────
            Item {
                id: kopf
                width: parent.width
                height: 32

                Rectangle {
                    width: 32
                    height: parent.height
                    topLeftRadius: Theme.panelRadius
                    color: card.finished && card.position === 1 ? (area.ex.p1Bg || Theme.accent)
                                                                : (area.ex.posBg || "#14151a")
                    Behavior on color { ColorAnimation { duration: 300 } }
                    Text {
                        anchors.centerIn: parent
                        text: card.position > 0 ? card.position : "–"
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 16; weight: Font.Bold; features: ({ "tnum": 1 }) }
                    }
                }
                Image {
                    x: 40
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: card.teamLogo ? Theme.teamLogo(card.teamLogo) : ""
                    visible: card.teamLogo !== ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize { width: 40; height: 40 }
                    smooth: true
                    mipmap: true
                }
                Text {
                    x: 68
                    width: parent.width - 68 - 30
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.name
                    elide: Text.ElideRight
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 17; weight: Font.Bold; letterSpacing: 0.5
                           capitalization: Font.AllUppercase }
                }
                Text {
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    visible: card.tyreIcon !== ""
                    text: card.tyreIcon
                    color: F1.reifenFarbe(card.tyreIcon, Theme.textMuted)
                    font { family: Theme.display; pixelSize: 15; weight: Font.Black }
                }
                Rectangle {                          // Teamfarbe unter dem Kopf
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 2
                    color: card.teamColor
                }
            }

            // ── Zeit links, Ziel bzw. Delta rechts ──────────────────────────
            Text {
                x: 14
                y: kopf.height + 4
                text: card.ticking
                      ? F1.zehntel(card.timeBase + Math.max(0, area.clock - card.timeAt) / 1000)
                      : (card.staticTime > 0 ? Fmt.time(card.staticTime) : "–")
                color: card.zeitFarbe
                font { family: Theme.display; pixelSize: 32; weight: Font.Bold; features: ({ "tnum": 1 }) }
            }
            Column {
                anchors { right: parent.right; rightMargin: 14 }
                width: 150
                y: kopf.height + 7
                visible: card.finished ? card.hasFinal : card.targetTime > 0
                Text {
                    anchors.right: parent.right
                    text: card.finished ? F1.delta(card.finalDelta) : Fmt.time(card.targetTime)
                    color: !card.finished ? Theme.textMain
                         : card.finalDelta <= 0 ? (area.ex.gain || "#2ad04b") : "#ffd200"
                    font { family: Theme.display; pixelSize: 18; weight: Font.Bold; features: ({ "tnum": 1 }) }
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    text: card.targetName
                    color: Theme.textMuted
                    font { family: Theme.display; pixelSize: 12; weight: Font.DemiBold; letterSpacing: 1
                           capitalization: Font.AllUppercase }
                }
            }

            // ── Sektoren ────────────────────────────────────────────────────
            Row {
                x: 12
                width: parent.width - 24
                anchors { bottom: parent.bottom; bottomMargin: 6 }
                spacing: 8
                Repeater {
                    model: 3
                    Item {
                        required property int index
                        readonly property string cls: card.sectorClasses && card.sectorClasses[index]
                                                      ? card.sectorClasses[index] : ""
                        width: (parent.width - 16) / 3
                        height: 20
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "S" + (index + 1)
                            color: parent.cls !== "" ? Fmt.sectorColor(parent.cls, false) : Theme.textMuted
                            font { family: Theme.display; pixelSize: 12; weight: Font.Bold; letterSpacing: 1 }
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 3
                            radius: 1
                            color: parent.cls !== "" ? Fmt.sectorColor(parent.cls, false) : Qt.rgba(1, 1, 1, 0.14)
                        }
                    }
                }
            }
        }
    }
}
