// Der Meldungs-Banner oben.
//
// Vorlage: #race-msg in static/parts/racemsg.css, Ablauf in _msgbanner.js.
// Rennleitung UND Undercut-Alarm laufen hier durch - wie im Gesamt-Overlay, wo es
// ebenfalls nur ein #race-msg gibt. Die Warteschlange steckt in hud/parts.py.

import QtQuick
import ".."

Item {
    id: banner

    readonly property var src: Kers.banner

    width: pill.width
    height: 50

    Rectangle {
        id: pill
        height: parent.height
        width: content.implicitWidth + 36                // padding: 0 18px
        radius: Theme.panelRadius
        color: Theme.panelBg
        border.width: 1
        border.color: Theme.panelBorder

        // border-left: var(--accent) solid var(--rm-accent)
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.accentWidth
            color: banner.src.accent
            topLeftRadius: Theme.panelRadius
            bottomLeftRadius: Theme.panelRadius
        }

        Row {
            id: content
            anchors { verticalCenter: parent.verticalCenter; left: parent.left
                      leftMargin: 18 }
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: banner.src.icon
                color: banner.src.accent
                font.pixelSize: 20
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: banner.src.text
                color: Theme.textMain
                font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold
                       capitalization: Font.AllUppercase }
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.7)
            }
        }
    }

    // Einblenden: gleichzeitig aufblenden und 18 px hochgleiten.
    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : -18)
    property real baseY: 0
    Behavior on opacity { NumberAnimation { duration: 350 } }
    Behavior on y {
        NumberAnimation {
            duration: 450
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
        }
    }
}
