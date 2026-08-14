// Onboard-Telemetrie des Kamera-Fahrers.
// Vorlage: #onboard in static/parts/onboard.css, Ablauf in onboard.js.
//
// Quali: der Fahrer, auf den die Kamera schaut. Rennen: nur im Safety-Car-Fenster,
// und dann immer P1. Die Auswahl trifft hud/parts.py.

import QtQuick
import ".."

Rectangle {
    id: ob

    readonly property var src: Kers.onboard

    width: 230
    height: head.height + bodyCol.implicitHeight + 24
    radius: Theme.panelRadius
    color: Theme.panelBg
    border.width: 1
    border.color: Theme.panelBorder
    clip: true

    visible: src.isVisible

    // border-top in Teamfarbe - rundet oben mit, siehe AccentTop.qml
    AccentTop {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        color: ob.src.teamColor
        radius: ob.radius
        z: 2
    }

    // ── Kopf ────────────────────────────────────────────────────────────────
    Rectangle {
        id: head
        width: parent.width
        height: 32
        // ⚠ Ecken hier noch einmal setzen: `clip: true` auf der Karte beschneidet
        // in Qt Quick nur auf das Rechteck und ignoriert die Rundung - sonst
        // deckt dieser Verlauf die oberen Ecken zu. Gleiche Stelle wie in
        // Battles.qml und PitCards.qml.
        topLeftRadius: ob.radius
        topRightRadius: ob.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.headBg1 }
            GradientStop { position: 1.0; color: Theme.headBg2 }
        }

        Row {
            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
            spacing: 8

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                source: ob.src.teamLogo ? Theme.teamLogo(ob.src.teamLogo) : ""
                visible: ob.src.teamLogo !== ""
                fillMode: Image.PreserveAspectFit
                sourceSize { width: 44; height: 44 }
                smooth: true
                // Breite Logos schrumpfen stark - siehe TowerRow.qml.
                mipmap: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 30 - posLabel.width - 16
                text: ob.src.name
                color: Theme.textMain
                elide: Text.ElideRight
                font { family: Theme.sans; pixelSize: 14; weight: Font.DemiBold
                       capitalization: Font.AllUppercase }
            }
            Text {
                id: posLabel
                anchors.verticalCenter: parent.verticalCenter
                text: "P" + ob.src.position
                color: "#cfd0d8"
                font { family: Theme.display; pixelSize: 17; weight: Font.DemiBold }
            }
        }
    }

    // ── Koerper ─────────────────────────────────────────────────────────────
    Column {
        id: bodyCol
        anchors { top: head.bottom; left: parent.left; right: parent.right
                  leftMargin: 14; rightMargin: 14; topMargin: 11 }
        spacing: 8

        // Tempo und Gang
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            // ⚠ Die Grundlinie gibt die GROESSTE Schrift vor, nicht die kleinste.
            // Vorher hingen Tempo (46 px) und Gang (34 px) an der Grundlinie von
            // "km/h" (14 px). Der Row setzt "km/h" oben an, seine Grundlinie liegt
            // also rund 13 px unter der Oberkante - die 46-px-Zahl wurde daran
            // hochgezogen und stand mit ihrer Oberkante weit ueber der Zeile.
            // Jetzt bestimmt das Tempo die Zeilenhoehe, und die beiden kleineren
            // Texte haengen sich an dessen Grundlinie.
            Text {
                id: tempo
                text: ob.src.speed
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 46; weight: Font.Bold
                       features: ({ "tnum": 1 }) }
            }
            Text {
                id: unit
                anchors.baseline: tempo.baseline
                text: "km/h"
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 14; weight: Font.DemiBold }
            }
            Text {
                anchors.baseline: tempo.baseline
                leftPadding: 12
                text: ob.src.gear
                color: "#2fd9ff"
                font { family: Theme.display; pixelSize: 34; weight: Font.Bold }
            }
        }

        // Gas und Bremse
        Column {
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { label: "GAS", value: ob.src.throttle,
                      c1: "#0a8f3c", c2: "#27e06a" },
                    { label: "BREMSE", value: ob.src.brake,
                      c1: "#a10f0f", c2: "#ff3b30" }
                ]
                Rectangle {
                    required property var modelData
                    width: bodyCol.width
                    height: 13
                    radius: 7
                    color: Qt.rgba(1, 1, 1, 0.07)
                    clip: true

                    Rectangle {
                        height: parent.height
                        width: parent.width * Math.max(0, Math.min(1, modelData.value))
                        radius: 7
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: modelData.c1 }
                            GradientStop { position: 1.0; color: modelData.c2 }
                        }
                        Behavior on width { NumberAnimation { duration: 90 } }
                    }
                    Text {
                        anchors { left: parent.left; leftMargin: 8
                                  verticalCenter: parent.verticalCenter }
                        text: modelData.label
                        color: Qt.rgba(1, 1, 1, 0.85)
                        font { family: Theme.sans; pixelSize: 9; weight: Font.Black
                               letterSpacing: 1 }
                    }
                }
            }
        }

        // Sektor-Ampel: drei Segmente, gefaerbt wie im Tower.
        Row {
            width: parent.width
            spacing: 6
            visible: ob.src.ampVisible

            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    readonly property string cls: ob.src.amp[index] || ""
                    width: (bodyCol.width - 12) / 3
                    height: 22
                    radius: 5
                    color: cls === "sp" ? "#b14bff" : cls === "sg" ? "#00d56a"
                         : cls === "sy" ? "#ffd000" : Qt.rgba(1, 1, 1, 0.07)
                    border.width: cls === "run" ? 1.5 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.3)
                    Behavior on color { ColorAnimation { duration: 200 } }

                    Text {
                        anchors.centerIn: parent
                        text: "S" + (index + 1)
                        color: parent.cls === "sg" ? "#006633"
                             : parent.cls === "sy" ? "#444433"
                             : parent.cls === "sp" ? "#ffffff" : Theme.textMuted
                        font { family: Theme.display; pixelSize: 13; letterSpacing: 1 }
                    }
                }
            }
        }

        // Delta-Balken zur persoenlichen Bestrunde (nur Quali auf dem Hotlap).
        Row {
            width: parent.width
            spacing: 8
            visible: ob.src.deltaVisible

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                text: "Δ PB"
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 9; weight: Font.Black
                       letterSpacing: 1 }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: bodyCol.width - 30 - 52 - 16
                height: 10
                radius: 5
                color: Qt.rgba(1, 1, 1, 0.07)
                clip: true

                // Der Balken waechst aus der Mitte: nach links = schneller.
                Rectangle {
                    readonly property real pct: ob.src.hasDelta
                        ? Math.min(Math.abs(ob.src.delta) / 0.75, 1) * 0.5 : 0
                    height: parent.height
                    radius: 5
                    x: parent.width * (ob.src.delta <= 0 ? 0.5 - pct : 0.5)
                    width: parent.width * pct
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0
                                       color: ob.src.delta <= 0 ? "#0a8f3c" : "#ff3b30" }
                        GradientStop { position: 1.0
                                       color: ob.src.delta <= 0 ? "#27e06a" : "#a10f0f" }
                    }
                    Behavior on width { NumberAnimation { duration: 250 } }
                    Behavior on x { NumberAnimation { duration: 250 } }
                }
                Rectangle {          // Mittelmarke
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 2
                    height: parent.height
                    color: Qt.rgba(1, 1, 1, 0.35)
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 52
                horizontalAlignment: Text.AlignRight
                text: ob.src.hasDelta
                      ? (ob.src.delta <= 0 ? "−" : "+") + Math.abs(ob.src.delta).toFixed(3)
                      : "—"
                color: !ob.src.hasDelta ? "#9aa0aa"
                     : ob.src.delta <= 0 ? "#27e06a" : "#ff5a5a"
                font { family: Theme.display; pixelSize: 17; weight: Font.DemiBold
                       features: ({ "tnum": 1 }) }
            }
        }
    }
}
