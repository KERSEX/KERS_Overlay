// Reifen wie das Pirelli-Symbol im Fernsehen (Design "F1 TV", ab 0.3.1, KERS'
// Wunsch nach seiner Vorlage): dunkle runde Scheibe, darauf zwei dicke Boegen
// links und rechts in der Farbe der Mischung - oben und unten offen -, in der
// Mitte das Alter in Runden.
//
// Die Farbe sagt die Mischung (S rot, M gelb, H weiss, I gruen, W blau), deshalb
// steht kein Buchstabe dabei. Unbekannte Mischung: nur ein Strich.
//
//     F1Reifen { compound: "S"; alter: 12 }

import QtQuick
import QtQuick.Shapes
import ".."
import "F1.js" as F1

Item {
    id: reifen

    property string compound: ""
    property int alter: 0

    readonly property bool bekannt: compound !== "" && "SMHIW".indexOf(compound) >= 0
    readonly property color farbe: F1.reifenFarbe(compound, Theme.textMuted)

    // Masse relativ zum Durchmesser, abgenommen an der Vorlage
    readonly property real d: Math.min(width, height)
    readonly property real strich: d * 0.11          // Dicke der Boegen
    readonly property real bogenR: d * 0.37          // Radius der Bogenmitte
    readonly property real bogen: 116                // Grad je Bogen -> oben/unten je 64 Grad offen

    implicitWidth: 28
    implicitHeight: 28

    Rectangle {                                       // die Reifenscheibe
        anchors.centerIn: parent
        width: reifen.d
        height: reifen.d
        radius: reifen.d / 2
        visible: reifen.bekannt
        color: Theme.extra.tyreBg || "#0b0b0e"
    }

    Shape {
        anchors.fill: parent
        visible: reifen.bekannt
        preferredRendererType: Shape.CurveRenderer

        ShapePath {                                   // links
            strokeColor: reifen.farbe
            strokeWidth: reifen.strich
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            PathAngleArc {
                centerX: reifen.width / 2
                centerY: reifen.height / 2
                radiusX: reifen.bogenR
                radiusY: reifen.bogenR
                startAngle: 180 - reifen.bogen / 2
                sweepAngle: reifen.bogen
            }
        }
        ShapePath {                                   // rechts
            strokeColor: reifen.farbe
            strokeWidth: reifen.strich
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            PathAngleArc {
                centerX: reifen.width / 2
                centerY: reifen.height / 2
                radiusX: reifen.bogenR
                radiusY: reifen.bogenR
                startAngle: -reifen.bogen / 2
                sweepAngle: reifen.bogen
            }
        }
    }

    Text {
        anchors.centerIn: parent
        text: reifen.bekannt ? reifen.alter : "–"
        color: reifen.bekannt ? "#ffffff" : Theme.textMuted
        // Je mehr Ziffern, desto kleiner - sonst stoesst die Zahl an die Boegen.
        font { family: Theme.display; weight: Font.Bold; features: ({ "tnum": 1 })
               pixelSize: Math.round(reifen.d * (reifen.alter >= 100 ? 0.33 : reifen.alter >= 10 ? 0.42 : 0.5)) }
    }
}
