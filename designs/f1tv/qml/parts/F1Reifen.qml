// Reifen wie im Fernsehen: zwei Halbkreise in der Farbe der Mischung, dazwischen
// das Alter in Runden - "(12)" (Design "F1 TV", ab 0.3.1, KERS' Wunsch).
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
    readonly property real r: height / 2          // Radius der Halbkreise
    readonly property real strich: 2.2

    implicitHeight: 20
    // Breit genug fuer zwei Ziffern; bei dreistelligem Alter waechst das Symbol mit.
    implicitWidth: Math.max(32, zahl.implicitWidth + 2 * r)

    Shape {
        anchors.fill: parent
        visible: reifen.bekannt
        preferredRendererType: Shape.CurveRenderer

        ShapePath {                               // links: (
            strokeColor: reifen.farbe
            strokeWidth: reifen.strich
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: reifen.r
            startY: reifen.strich / 2
            PathArc {
                x: reifen.r
                y: reifen.height - reifen.strich / 2
                radiusX: reifen.r - reifen.strich / 2
                radiusY: reifen.r - reifen.strich / 2
                direction: PathArc.Counterclockwise
            }
        }
        ShapePath {                               // rechts: )
            strokeColor: reifen.farbe
            strokeWidth: reifen.strich
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: reifen.width - reifen.r
            startY: reifen.strich / 2
            PathArc {
                x: reifen.width - reifen.r
                y: reifen.height - reifen.strich / 2
                radiusX: reifen.r - reifen.strich / 2
                radiusY: reifen.r - reifen.strich / 2
                direction: PathArc.Clockwise
            }
        }
    }

    Text {
        id: zahl
        anchors.centerIn: parent
        text: reifen.bekannt ? reifen.alter : "–"
        color: reifen.bekannt ? Theme.textMain : Theme.textMuted
        font { family: Theme.display; pixelSize: 13; weight: Font.Bold; features: ({ "tnum": 1 }) }
    }
}
