// WM-Stand im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
//   ┌──────────────────────────────────────┐
//   │ FAHRERWERTUNG                          │
//   │ ● LIVE · WENN DAS RENNEN SO ENDET      │
//   ├──┬───────────────────────────────────┤
//   │ 1│▌[Logo] FINN CHEST    ▲2  +25   125 │
//   └──┴───────────────────────────────────┘
//
// Wie der Tower: Positionsspalte (P1 rot), Teamfarbe, Logo, voller Name. Rechts
// die Punkte, davor in Gruen die Punkte des laufenden Rennens und ein Pfeil, um
// wie viele Plaetze sich jemand damit verschiebt. Fahrer oder Teams je nach
// Einstellung champ_view. Ein MANUELLES Panel: es erscheint nur, wenn es in
// /regie eingeschaltet ist. Ersetzt parts/Championship.qml (gleiche Daten,
// Kers.championship).

import QtQuick
import ".."

Item {
    id: champ

    readonly property var src: Kers.championship
    readonly property var ex: Theme.extra
    readonly property int rowH: 32

    width: 360
    height: kopf.height + liste.height + 6

    opacity: src.isVisible ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 350 } }

    Rectangle {
        anchors.fill: parent
        radius: Theme.panelRadius
        color: Theme.panelBg
        border.width: 1
        border.color: Theme.panelBorder
    }

    // ── Kopf ─────────────────────────────────────────────────────────────────
    Column {
        id: kopf
        width: parent.width

        Item {
            width: parent.width
            height: 42
            Text {
                anchors { left: parent.left; leftMargin: 14; right: parent.right; rightMargin: 14
                          verticalCenter: parent.verticalCenter }
                text: champ.src.title
                elide: Text.ElideRight
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 19; weight: Font.Black; letterSpacing: 0.8
                       capitalization: Font.AllUppercase }
            }
        }
        Rectangle {
            width: parent.width
            height: 26
            color: champ.ex.lapBar || Theme.headerRowBg
            Row {
                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: champ.src.live
                    width: 8
                    height: 8
                    radius: 4
                    color: champ.ex.redFlag || "#e10600"
                    SequentialAnimation on opacity {
                        running: champ.src.live && champ.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700 }
                        NumberAnimation { to: 1.0; duration: 700 }
                    }
                }
                Text {
                    text: champ.src.live ? "LIVE · WENN DAS RENNEN SO ENDET"
                                         : (champ.src.teamView ? "TEAMS" : "FAHRER")
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 12; weight: Font.Bold; letterSpacing: 1 }
                }
            }
            Text {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "PKT"
                color: Theme.textMuted
                font { family: Theme.display; pixelSize: 12; weight: Font.Bold; letterSpacing: 1 }
            }
        }
    }

    // ── Zeilen ───────────────────────────────────────────────────────────────
    Column {
        id: liste
        anchors.top: kopf.bottom
        width: parent.width

        Repeater {
            model: champ.src.rows
            Item {
                id: zeile
                required property var modelData
                required property int index
                readonly property bool letzte: index === champ.src.rows.length - 1
                width: liste.width
                height: champ.rowH

                Rectangle {
                    width: 34
                    height: parent.height
                    bottomLeftRadius: zeile.letzte ? Theme.panelRadius : 0
                    color: zeile.modelData.pos === 1 ? (champ.ex.p1Bg || Theme.accent) : (champ.ex.posBg || "#14151a")
                    Text {
                        anchors.centerIn: parent
                        text: zeile.modelData.pos
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 15; weight: Font.DemiBold; features: ({ "tnum": 1 }) }
                    }
                }
                Rectangle {                      // Teamfarbe
                    x: 34
                    width: 3
                    height: parent.height
                    color: zeile.modelData.color
                }
                Image {
                    x: 44
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    source: zeile.modelData.logo ? Theme.teamLogo(zeile.modelData.logo) : ""
                    visible: !!zeile.modelData.logo
                    fillMode: Image.PreserveAspectFit
                    sourceSize { width: 40; height: 40 }
                    smooth: true
                    mipmap: true
                }
                Text {
                    x: 71
                    width: rechts.x - 71 - 8
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: zeile.modelData.name
                    elide: Text.ElideRight
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 15; weight: Font.Bold; letterSpacing: 0.3
                           capitalization: Font.AllUppercase }
                }
                Row {
                    id: rechts
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 8
                    Text {                       // Plaetze gewonnen/verloren durch das laufende Rennen
                        anchors.verticalCenter: parent.verticalCenter
                        visible: zeile.modelData.change !== 0 && zeile.modelData.change !== undefined
                        text: (zeile.modelData.change > 0 ? "▲" : "▼") + Math.abs(zeile.modelData.change)
                        color: zeile.modelData.change > 0 ? (champ.ex.gain || "#2ad04b") : (champ.ex.loss || "#e10600")
                        font { family: Theme.display; pixelSize: 12; weight: Font.Bold }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: zeile.modelData.livePoints > 0
                        text: "+" + zeile.modelData.livePoints
                        color: champ.ex.gain || "#2ad04b"
                        font { family: Theme.display; pixelSize: 13; weight: Font.DemiBold; features: ({ "tnum": 1 }) }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        horizontalAlignment: Text.AlignRight
                        text: zeile.modelData.points
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 17; weight: Font.Bold; features: ({ "tnum": 1 }) }
                    }
                }
            }
        }
    }
}
