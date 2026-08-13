// Die Startgantry: fuenf Saeulen mit je zwei Lampen.
// Vorlage: #start-lights in static/parts/lights.css, Ablauf in lights.js.

import QtQuick
import ".."

Column {
    id: lights

    readonly property var src: Kers.lights

    spacing: 16
    visible: opacity > 0
    opacity: src.isVisible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Rectangle {
        id: gantry
        anchors.horizontalCenter: parent.horizontalCenter
        width: cols.implicitWidth + 44                   // padding: 18px 22px
        height: cols.implicitHeight + 36
        radius: 14
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, 0.10)
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0c0c10" }
            GradientStop { position: 1.0; color: "#17171d" }
        }

        Row {
            id: cols
            anchors.centerIn: parent
            spacing: 14

            Repeater {
                model: 5
                Column {
                    required property int index
                    spacing: 10
                    // Die Lichter gehen saeulenweise von links an; bei "Lights Out"
                    // sind alle wieder aus.
                    readonly property bool on: !lights.src.lightsOut
                                               && index < lights.src.lit

                    Repeater {
                        model: 2
                        Rectangle {
                            width: 46
                            height: 46
                            radius: 23
                            border.width: 2
                            border.color: Qt.rgba(0, 0, 0, 0.6)
                            gradient: Gradient {
                                GradientStop { position: 0.0
                                               color: parent.parent.on ? "#ff6b5a" : "#3a0a0a" }
                                GradientStop { position: 0.55
                                               color: parent.parent.on ? "#e10600" : "#220606" }
                                GradientStop { position: 1.0
                                               color: parent.parent.on ? "#8a0400" : "#180404" }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        id: msg
        anchors.horizontalCenter: parent.horizontalCenter
        text: "LIGHTS OUT"
        color: "#26ff8f"
        opacity: lights.src.lightsOut ? 1 : 0
        font { family: Theme.display; pixelSize: 44; weight: Font.Bold
               letterSpacing: 5; capitalization: Font.AllUppercase }

        // lights-out-pop: kurz ueberschiessen, dann einrasten.
        NumberAnimation on scale {
            running: lights.src.lightsOut
            from: 0.4
            to: 1.0
            duration: 500
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 1.4, 0.5, 1, 1, 1]
        }
    }
}
