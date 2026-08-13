// Lower-Third: das Namens-Tag beim Kamerawechsel.
// Vorlage: #lower-third in static/parts/lowerthird.css, Ablauf in lowerthird.js.
//
// Der Massstab 1,14 steckt im Original im transform - hier als `scale`. Ankerpunkt
// ist die untere Mitte, das Tag waechst also nach oben.

import QtQuick
import ".."

Item {
    id: lt

    readonly property var src: Kers.lowerThird
    property real baseY: 0

    width: box.width * scale
    height: box.height * scale
    transformOrigin: Item.BottomLeft
    scale: 1.14

    opacity: src.isVisible ? 1 : 0
    y: baseY + (src.isVisible ? 0 : 14)
    Behavior on opacity { NumberAnimation { duration: 350 } }
    Behavior on y {
        NumberAnimation {
            duration: 450
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.2, 0.9, 0.3, 1, 1, 1]
        }
    }

    Row {
        id: box
        spacing: 0

        // Position, in Teamfarbe hinterlegt
        Rectangle {
            width: posText.implicitWidth + 32
            height: body.height
            color: lt.src.teamColor
            topLeftRadius: Theme.panelRadius
            bottomLeftRadius: Theme.panelRadius

            Text {
                id: posText
                anchors.centerIn: parent
                text: "P" + lt.src.position
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 30; weight: Font.Bold }
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.55)
            }
        }

        // Name, Team, Reifenalter, Stint-Strip
        Rectangle {
            id: body
            width: Math.max(190, bodyCol.implicitWidth + 32)
            height: bodyCol.implicitHeight + 14
            color: Theme.panelBg

            Column {
                id: bodyCol
                anchors { left: parent.left; top: parent.top
                          leftMargin: 16; topMargin: 7 }
                spacing: 1

                Text {
                    text: lt.src.name
                    color: Theme.textMain
                    font { family: Theme.sans; pixelSize: 17; weight: Font.Bold
                           capitalization: Font.AllUppercase }
                }

                Row {
                    spacing: 5
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15
                        height: 15
                        source: lt.src.tyreIcon ? Theme.tyre(lt.src.tyreIcon) : ""
                        visible: lt.src.tyreIcon !== ""
                        fillMode: Image.PreserveAspectFit
                        sourceSize { width: 30; height: 30 }
                        smooth: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: lt.src.team
                        color: Theme.textMuted
                        font { family: Theme.sans; pixelSize: 11; letterSpacing: 0.5
                               capitalization: Font.AllUppercase }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 4
                        text: lt.src.ageText
                        color: "#cfd0d8"
                        font { family: Theme.sans; pixelSize: 11; letterSpacing: 0.5
                               capitalization: Font.AllUppercase }
                    }
                }

                // Reifenstrategie: je Stint ein Reifenbild (oder der Buchstabe auf
                // farbigem Feld) und die gefahrenen Runden dahinter.
                Row {
                    topPadding: 5
                    spacing: 10
                    visible: lt.src.stints.length > 0

                    Repeater {
                        model: lt.src.stints
                        Row {
                            required property var modelData
                            spacing: 3

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.icon !== ""
                                width: visible ? 20 : 0
                                height: 20
                                source: modelData.icon ? Theme.tyre(modelData.icon) : ""
                                fillMode: Image.PreserveAspectFit
                                sourceSize { width: 40; height: 40 }
                                smooth: true
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: modelData.icon === ""
                                width: visible ? 20 : 0
                                height: 16
                                radius: 4
                                color: modelData.color
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.letter
                                    color: Qt.rgba(0, 0, 0, 0.72)
                                    font { family: Theme.display; pixelSize: 11
                                           weight: Font.Black }
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.laps
                                color: "#cfd0d8"
                                font { family: Theme.display; pixelSize: 14
                                       weight: Font.DemiBold }
                            }
                        }
                    }
                }
            }
        }

        // Abstand zum Vordermann
        Rectangle {
            width: gapText.implicitWidth + 32
            height: body.height
            color: Theme.panelBg
            topRightRadius: Theme.panelRadius
            bottomRightRadius: Theme.panelRadius

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            Text {
                id: gapText
                anchors.centerIn: parent
                text: lt.src.gapText
                color: "#ffcc00"
                font { family: Theme.display; pixelSize: 26; weight: Font.DemiBold
                       features: ({ "tnum": 1 }) }
            }
        }
    }
}
