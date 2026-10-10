// Quali: Hotlap-Karten im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   [P] [Logo] NACHNAME                       S
//   ─────────────────── Teamfarbe ───────────────
//   1:31.3                          1:31.529
//                                   HAMILTON
//      S1          S2          S3
//   ▔▔▔▔▔▔▔    ▔▔▔▔▔▔▔    ▔▔▔▔▔▔▔   (gruen/gelb/lila je Sektor)
//
// Ersetzt parts/Hotlaps.qml. Gleiche Daten (Kers.hotlaps.boxes), gleiche Groessen-
// Schnittstelle zu Overlay.qml (ein Row, dessen Breite/Hoehe zaehlt).

import QtQuick
import ".."
import "../Fmt.js" as Fmt

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

    /** Wer haelt die Pole? Steht rechts in jeder Karte unter der Zielzeit. */
    readonly property string poleName: Kers.session.poleName

    function nachname(n) {
        var w = String(n || "").trim().split(/\s+/);
        var letzt = w[w.length - 1];
        return (w.length > 1 && /^[^0-9#]+$/.test(letzt)) ? letzt : String(n || "");
    }
    function reifenFarbe(c) {
        switch (c) {
        case "S": return "#ff3333";
        case "M": return "#ffd400";
        case "H": return "#f2f2f2";
        case "I": return "#2bd04b";
        case "W": return "#2e8bff";
        }
        return Theme.textMuted;
    }
    /** Laufende Zeit mit Zehnteln wie im Fernsehen: 1:31.3 */
    function zehntel(s) {
        var t = Fmt.time(Math.floor(s * 10) / 10);
        return t.length > 2 ? t.slice(0, -2) : t;
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

            width: 330
            height: 114
            anchors.bottom: parent ? parent.bottom : undefined
            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: 400 } }

            Rectangle {
                anchors.fill: parent
                radius: Theme.panelRadius
                color: Theme.panelBg
                border.width: 1
                border.color: Theme.panelBorder
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
                    color: area.ex.posBg || "#14151a"
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
                    text: area.nachname(card.name)
                    elide: Text.ElideRight
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 17; weight: Font.Bold; letterSpacing: 0.5
                           capitalization: Font.AllUppercase }
                }
                Text {
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    visible: card.tyreIcon !== ""
                    text: card.tyreIcon
                    color: area.reifenFarbe(card.tyreIcon)
                    font { family: Theme.display; pixelSize: 15; weight: Font.Black }
                }
                Rectangle {                          // Teamfarbe unter dem Kopf
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 2
                    color: card.teamColor
                }
            }

            // ── Zeit links, Ziel rechts ─────────────────────────────────────
            Text {
                x: 14
                y: kopf.height + 4
                text: card.ticking
                      ? area.zehntel(card.timeBase + Math.max(0, area.clock - card.timeAt) / 1000)
                      : (card.staticTime > 0 ? Fmt.time(card.staticTime) : "–")
                color: card.invalid ? "#ff5a5a" : Theme.textMain
                font { family: Theme.display; pixelSize: 32; weight: Font.Bold; features: ({ "tnum": 1 }) }
            }
            Column {
                anchors { right: parent.right; rightMargin: 14 }
                y: kopf.height + 7
                visible: Kers.session.poleTime > 0
                Text {
                    anchors.right: parent.right
                    text: Fmt.time(Kers.session.poleTime)
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 18; weight: Font.Bold; features: ({ "tnum": 1 }) }
                }
                Text {
                    anchors.right: parent.right
                    text: area.nachname(area.poleName)
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
