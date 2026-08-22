// Eine Zeile in einer Battle-Box.
// Vorlage: .battle-row in static/parts/battles.css.
//
// Wie im Tower positioniert sich die Zeile selbst ueber `slot` und gleitet, statt
// dass Delegates umgebaut werden - siehe den Kommentar in TowerRow.qml.

import QtQuick
import ".."

Item {
    id: row

    required property int slot
    required property int position
    required property string name
    required property color teamColor
    required property string tyreIcon
    required property int tyreStamp
    required property real gap
    required property bool isLead
    required property real barWidth
    required property color barColor
    required property string fresh

    // Die unterste Zeile rundet mit der Karte ab (s. Kommentar bei der Teamkante).
    // Wie im Tower ueber die Gesamtzahl von aussen, nicht ueber `index`: bei einem
    // Delegate mit required properties ist index nicht verlaesslich sichtbar.
    property int lastSlot: -1
    readonly property bool isLast: slot === lastSlot

    height: 44
    y: slot * 44

    Behavior on y {
        enabled: row.settled
        NumberAnimation {
            duration: 550
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.22, 0.61, 0.36, 1, 1, 1]
        }
    }

    property bool settled: false
    opacity: 0
    Component.onCompleted: {
        opacity = 1;
        settled = true;
    }
    Behavior on opacity { NumberAnimation { duration: 400 } }

    // Teamfarbe: Kante links plus auslaufender Schleier ueber die Zeile.
    //
    // ⚠ In der LETZTEN Zeile muss die Kante der Rundung der Karte folgen. Sie ist
    // nur 3 px breit, die Karte hat aber 10 px Radius - ganz unten ist die Karte
    // also 10 px eingezogen und die Kante stand als eckige Nase heraus (`clip` der
    // Karte beschneidet nur rechteckig, nicht auf die Rundung). Ein
    // bottomLeftRadius direkt auf der Kante hilft nicht: Qt begrenzt ihn auf die
    // halbe Breite, also 1,5 px. Deshalb ein 10 px breites, korrekt gerundetes
    // Rechteck, von dem der Container nur die linken 3 px durchlaesst.
    Item {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: Theme.accentWidth
        clip: true
        Rectangle {
            // ⚠ Mindestens die DOPPELTE Radiusbreite. Qt kappt einen Radius auf die
            // Haelfte der kleineren Kantenlaenge - bei 10 px Breite waeren aus
            // radius 10 also 5 geworden, und die Kante haette sich anders eingezogen
            // als die Karte: zwischen beiden klafften Loecher.
            width: Theme.panelRadius * 2
            height: parent.height
            color: Kers.settings.rowColor !== "" ? Kers.settings.rowColor : row.teamColor
            bottomLeftRadius: row.isLast ? Theme.panelRadius : 0
        }
    }
    Rectangle {
        anchors.fill: parent
        opacity: 0.10
        bottomLeftRadius: row.isLast ? Theme.panelRadius : 0
        bottomRightRadius: row.isLast ? Theme.panelRadius : 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.00; color: row.teamColor }
            GradientStop { position: 0.42; color: "transparent" }
        }
    }
    Rectangle {          // box-shadow: inset 0 1px 0 rgba(255,255,255,0.05)
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Qt.rgba(1, 1, 1, 0.05)
    }

    // Der Abstandsbalken an der Oberkante: fuellt sich, je naeher der Vordermann ist.
    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: 8; rightMargin: 8 }
        height: 3
        radius: 2
        color: Qt.rgba(1, 1, 1, 0.07)
        visible: !row.isLead
        clip: true

        Rectangle {
            height: parent.height
            width: parent.width * row.barWidth
            radius: 2
            color: row.barColor
            Behavior on width { NumberAnimation { duration: 200 } }
            Behavior on color { ColorAnimation { duration: 300 } }
        }
    }

    Row {
        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
        spacing: 11

        Text {
            width: 24
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: row.position
            color: Theme.textMain
            font { family: Theme.display; pixelSize: 24; weight: Font.DemiBold }
        }

        Text {
            width: parent.width - 24 - 26 - right.width - 3 * 11
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: row.name
            color: Theme.textMain
            elide: Text.ElideRight
            font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold
                   letterSpacing: 0.3; capitalization: Font.AllUppercase }
            style: Theme.textOutline > 0 ? Text.Outline : Text.Raised
            styleColor: Theme.textStyleColor
        }

        Row {
            id: right
            height: parent.height
            spacing: 9

            // Frischere Reifen am Jaeger
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: row.fresh !== ""
                text: row.fresh
                color: "#00e676"
                font { family: Theme.display; pixelSize: 12; weight: Font.Bold }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 58
                horizontalAlignment: Text.AlignRight
                text: row.isLead ? "—" : "+" + row.gap.toFixed(3)
                color: row.isLead ? Qt.rgba(1, 1, 1, 0.22) : "#ffcc00"
                font { family: Theme.display; pixelSize: 21; weight: Font.DemiBold
                       letterSpacing: 0.5; features: ({ "tnum": 1 }) }
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26

                Image {
                    id: tyre
                    anchors.fill: parent
                    source: row.tyreIcon ? Theme.tyre(row.tyreIcon) : ""
                    visible: row.tyreIcon !== ""
                    fillMode: Image.PreserveAspectFit
                    sourceSize { width: 52; height: 52 }
                    smooth: true
                    transformOrigin: Item.Center
                }

                ParallelAnimation {
                    id: spin
                    NumberAnimation { target: tyre; property: "rotation"; from: -200
                                      to: 0; duration: 600; easing.type: Easing.Bezier
                                      easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1] }
                    NumberAnimation { target: tyre; property: "scale"; from: 0.35
                                      to: 1; duration: 600; easing.type: Easing.Bezier
                                      easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1] }
                }
            }
        }
    }

    onTyreStampChanged: if (tyreStamp > 0) spin.restart()
}
