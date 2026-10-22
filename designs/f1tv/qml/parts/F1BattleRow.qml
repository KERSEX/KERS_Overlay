// Eine Zeile einer F1-TV-Battle-Box (Design "F1 TV", ab 0.3.1).
//
//   [Pos] [Logo] KÜRZEL   −5 Rnd  +0.412  M
//   ▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔  (Abstandsbalken, fuellt sich, je naeher der Vordermann)
//
// Gleitet wie die Tower-Zeile ueber `slot` an ihren Platz.

import QtQuick
import ".."
import "F1.js" as F1

Item {
    id: row

    property int rowH: 34
    property int lastSlot: -1
    readonly property var ex: Theme.extra

    required property int slot
    required property int position
    required property string name
    required property color teamColor
    required property string teamLogo
    required property string tyreIcon
    required property real gap
    required property bool isLead
    required property real barWidth
    required property color barColor
    required property string fresh

    height: rowH
    y: slot * rowH
    property bool settled: false
    opacity: 0
    Component.onCompleted: {
        opacity = 1;
        settled = true;
    }
    Behavior on opacity { NumberAnimation { duration: 400 } }
    Behavior on y {
        enabled: row.settled
        NumberAnimation { duration: 550; easing.type: Easing.Bezier; easing.bezierCurve: [0.22, 0.61, 0.36, 1, 1, 1] }
    }

    // ── Position ─────────────────────────────────────────────────────────────
    Rectangle {
        width: 34
        height: parent.height
        color: row.position === 1 ? (row.ex.p1Bg || Theme.accent) : (row.ex.posBg || "#14151a")
        bottomLeftRadius: row.slot === row.lastSlot ? Theme.panelRadius : 0
        Text {
            anchors.centerIn: parent
            text: row.position
            color: Theme.textMain
            font { family: Theme.display; pixelSize: 15; weight: Font.DemiBold; features: ({ "tnum": 1 }) }
        }
    }

    Image {
        x: 41
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        source: row.teamLogo ? Theme.teamLogo(row.teamLogo) : ""
        visible: row.teamLogo !== ""
        fillMode: Image.PreserveAspectFit
        sourceSize { width: 40; height: 40 }
        smooth: true
        mipmap: true
    }

    Text {
        x: 68
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        text: F1.kuerzel(row.name)
        color: Theme.textMain
        font { family: Theme.display; pixelSize: 16; weight: Font.Bold; letterSpacing: 0.4
               capitalization: Font.AllUppercase }
    }

    // ── Rechts: frischere Reifen, Abstand, Reifen ────────────────────────────
    Row {
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: row.fresh !== ""
            text: row.fresh
            color: row.ex.gain || "#2ad04b"
            font { family: Theme.display; pixelSize: 12; weight: Font.Bold }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: !row.isLead
            text: "+" + row.gap.toFixed(3)
            color: Theme.textMain
            font { family: Theme.display; pixelSize: 15; weight: Font.Medium; features: ({ "tnum": 1 }) }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            horizontalAlignment: Text.AlignHCenter
            text: row.tyreIcon
            color: F1.reifenFarbe(row.tyreIcon, Theme.textMuted)
            font { family: Theme.display; pixelSize: 13; weight: Font.Black }
        }
    }

    // Abstandsbalken an der Unterkante - fuellt sich, je naeher der Vordermann ist.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 34 }
        height: 2
        color: "transparent"
        visible: !row.isLead
        Rectangle {
            height: parent.height
            width: parent.width * row.barWidth
            color: row.barColor
            Behavior on width { NumberAnimation { duration: 200 } }
        }
    }
}
