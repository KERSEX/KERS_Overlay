// VirtualCam (ab 0.3.3): Bild-im-Bild mit eigener 3D-Szene.
//
// Zeigt, was WOANDERS auf der Strecke passiert - nie ein Auto, das man im
// Spielbild sieht (Auswahl in src/hud/vcam.cpp). Steuerung aus der Regie
// (Kamera, Ziel) oder Auto-Regie. Kopf: Bildunterschrift + Kameraname,
// darunter das Bild mit Namensschildern.

import QtQuick
import KersCam
import ".."

Rectangle {
    id: vc

    readonly property var src: Kers.vcam

    width: 480
    height: kopf.height + bild.height + 1
    radius: Theme.panelRadius
    color: Theme.panelBg
    border.width: 1
    border.color: Theme.panelBorder
    clip: true

    opacity: src.isVisible ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 400 } }

    // ── Kopf ────────────────────────────────────────────────────────────────
    Rectangle {
        id: kopf
        width: parent.width
        height: 32
        // Ecken wiederholen - `clip` beschneidet nur aufs Rechteck (wie Onboard.qml).
        topLeftRadius: vc.radius
        topRightRadius: vc.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.headBg1 }
            GradientStop { position: 1.0; color: Theme.headBg2 }
        }

        Rectangle {                     // roter Punkt: "live"
            id: punkt
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            width: 8
            height: 8
            radius: 4
            color: "#ff3b30"
        }
        Text {
            anchors { left: punkt.right; leftMargin: 8; right: kameraText.left; rightMargin: 10
                      verticalCenter: parent.verticalCenter }
            text: vc.src.titel
            color: Theme.textMain
            elide: Text.ElideRight
            font { family: Theme.display; pixelSize: 16; weight: Font.Bold; letterSpacing: 0.5 }
        }
        Text {
            id: kameraText
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            text: vc.src.kamera
            color: Theme.textMuted
            font { family: Theme.sans; pixelSize: 11; weight: Font.DemiBold; letterSpacing: 1 }
        }
    }

    // ── Bild ────────────────────────────────────────────────────────────────
    VirtualCamView {
        id: bild
        anchors.top: kopf.bottom
        x: 1
        width: parent.width - 2
        height: Math.round(width * 9 / 16)
        quelle: vc.src
        // Bausteingroesse (Layout) mitnehmen - sonst wird die Textur hochgezogen und unscharf.
        fixedColorBufferWidth: Math.round(width * vc.scale * Screen.devicePixelRatio)
        fixedColorBufferHeight: Math.round(height * vc.scale * Screen.devicePixelRatio)

        // Namensschilder: Position + Name, Teamfarbe als Strich links.
        // Feste Plaetze statt Modell-Liste: die Liste kommt 30-120x je Sekunde neu,
        // ein Repeater darauf baute jedes Mal alle Schilder neu.
        Repeater {
            model: 6
            Rectangle {
                required property int index
                readonly property var s: vc.src.schilder[index] ?? null
                visible: s !== null
                x: s ? Math.round(s.x * bild.width - width / 2) : 0
                y: s ? Math.round(s.y * bild.height - height - 4) : 0
                width: schild.implicitWidth + 14
                height: 18
                radius: 3
                color: s && s.haupt ? "#e6101218" : "#a6101218"
                Rectangle {
                    width: 3
                    height: parent.height
                    radius: 1.5
                    color: parent.s ? parent.s.farbe : "transparent"
                }
                Text {
                    id: schild
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    text: parent.s ? parent.s.text : ""
                    color: parent.s && parent.s.haupt ? Theme.textMain : Theme.textMuted
                    font { family: Theme.sans; pixelSize: 11; weight: Font.DemiBold }
                }
            }
        }
    }
}
