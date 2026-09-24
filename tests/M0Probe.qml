// Probe-Szene fuer Meilenstein 0: beweist, dass die STATISCHE EXE alles kann,
// was die echte Szene braucht - bevor eine Zeile portiert wird.
//
// Zu sehen sein muss:
//   * Fenster durchsichtig (Desktop hinter dem Panel sichtbar)            -> Alphakanal
//   * Ueberschrift in Teko, Fliesstext in Inter                          -> Schriften aus qrc
//   * drei Podiumsziffern mit Metallverlauf und wanderndem Glanz         -> MultiEffect (Maske)
//   * Ferrari-Logo und ein Reifen-Icon                                    -> Bilder aus qrc
//   * ein rot pulsierender Fluegel (Damage-Icon als Form)                 -> MultiEffect (Maske + Animation)
//   * ein gelber Lichtpunkt, der um das VSC-Feld laeuft                   -> QtQuick.Shapes + ConicalGradient
//
// Start:  KERS_Subsystems.exe --probe      Esc beendet.

import QtQuick
import "."

Item {
    id: root

    Rectangle {
        x: 24; y: 24
        width: 620; height: 400
        radius: Theme.panelRadius
        color: Theme.panelBg
        border.color: Theme.panelBorder
        border.width: 1

        Column {
            x: 22; y: 14
            spacing: 12

            Text {
                text: "KERS SUBSYSTEMS – M0-PROBE"
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 44; weight: Font.Bold }
            }
            Text {
                text: "Inter 17 · Teko 44 · statisches Qt " + Qt.application.version
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 17 }
            }

            Row {
                spacing: 18

                MedalText {
                    text: "1"; rank: 1
                    font { family: Theme.display; pixelSize: 72; weight: Font.Bold }
                }
                MedalText {
                    text: "2"; rank: 2
                    font { family: Theme.display; pixelSize: 72; weight: Font.Bold }
                }
                MedalText {
                    text: "3"; rank: 3
                    font { family: Theme.display; pixelSize: 72; weight: Font.Bold }
                }

                Item { width: 12; height: 1 }

                Image {
                    width: 72; height: 72
                    source: Theme.teamLogo("Ferrari")
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    sourceSize: Qt.size(72, 72)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Image {
                    width: 52; height: 52
                    source: Theme.tyre("S")
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(52, 52)
                    anchors.verticalCenter: parent.verticalCenter
                }
                MaskedIcon {
                    width: 56; height: 36
                    source: Theme.damage("FWL")
                    color: "#ff3333"
                    blinking: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Item {
                width: 320; height: 64

                Rectangle {
                    anchors.fill: parent
                    radius: 12
                    color: Theme.rowBg
                }
                RunningBorder {
                    anchors.fill: parent
                    thickness: 4
                    frameRadius: 12
                    period: 3000
                    stops: [
                        { p: 0.00, c: "transparent" },
                        { p: 0.55, c: "transparent" },
                        { p: 0.85, c: "#ffe100" },
                        { p: 0.96, c: "#fff6a0" },
                        { p: 1.00, c: "transparent" }
                    ]
                }
                Text {
                    anchors.centerIn: parent
                    text: "VIRTUAL SAFETY CAR"
                    color: "#ffe100"
                    font { family: Theme.display; pixelSize: 30; weight: Font.Bold }
                }
            }

            Text {
                text: "Esc beendet die Probe"
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 13 }
            }
        }
    }
}
