// Ein Lichtpunkt, der am Rand entlanglaeuft.
//
// Zwei Stellen im Overlay benutzen das (static/parts/tower.css):
//   .leaderboard-container.vsc-active::after   Virtual Safety Car, gelb, 4 px, 4 s
//   .col-drs.boost-active::after               F1-26-Overtake-Mode, blau, 2 px, 6 s
//
// Im CSS ist es ein conic-gradient, dessen Winkel animiert wird, ausgeschnitten
// auf den Rand per mask-composite: exclude. Hier genauso: ein Kegelverlauf,
// maskiert mit einem Rechteck, das NUR seinen Rahmen deckt (Fuellung transparent,
// border deckend) - was ausserhalb des Rahmens liegt, faellt weg.
//
// ⚠ Der Kegelverlauf kommt aus QtQuick.Shapes und NICHT aus
// Qt5Compat.GraphicalEffects. Dessen ConicalGradient laesst sich in PySide6 nicht
// laden ("Cannot assign object of type QGfxSourceProxy to list property data") -
// die Shapes-Variante ist ohnehin die regulaer unterstuetzte.

import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

Item {
    id: ring

    property int thickness: 4
    property real frameRadius: 12
    property int period: 4000
    /** Farbstopps des Kometen als [{ p: Anteil, c: Farbe }, ...]. */
    property var stops: []

    // ── Der Kegelverlauf, ueber die ganze Flaeche ───────────────────────────
    Shape {
        id: sweep
        anchors.fill: parent
        visible: false
        layer.enabled: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: -1                       // kein Rand, nur Fuellung
            fillGradient: ConicalGradient {
                centerX: ring.width / 2
                centerY: ring.height / 2

                // Der umlaufende Winkel ist das @keyframes vsc-run bzw. mom-run.
                NumberAnimation on angle {
                    running: ring.visible
                    loops: Animation.Infinite
                    from: 0
                    to: 360
                    duration: ring.period
                }

                // Die Stopps kommen von aussen, damit dieselbe Datei fuer den
                // gelben VSC-Rahmen und die blaue MOM-Pille reicht. Feste Anzahl,
                // weil beide Aufrufer genau fuenf setzen - das spart einen
                // Instantiator, der zur Laufzeit Objekte in die Liste haengt.
                GradientStop { position: ring.stopAt(0).p; color: ring.stopAt(0).c }
                GradientStop { position: ring.stopAt(1).p; color: ring.stopAt(1).c }
                GradientStop { position: ring.stopAt(2).p; color: ring.stopAt(2).c }
                GradientStop { position: ring.stopAt(3).p; color: ring.stopAt(3).c }
                GradientStop { position: ring.stopAt(4).p; color: ring.stopAt(4).c }
            }

            startX: 0
            startY: 0
            PathLine { x: ring.width; y: 0 }
            PathLine { x: ring.width; y: ring.height }
            PathLine { x: 0; y: ring.height }
            PathLine { x: 0; y: 0 }
        }
    }

    /** Stopp Nr. n, oder ein unsichtbarer am Ende, falls der Aufrufer weniger setzt. */
    function stopAt(n) {
        return (stops && stops[n]) ? stops[n] : { p: 1.0, c: "transparent" };
    }

    // ── Die Maske: nur der Rahmen ist deckend ───────────────────────────────
    Rectangle {
        id: frame
        anchors.fill: parent
        color: "transparent"
        radius: ring.frameRadius
        border.width: ring.thickness
        border.color: "black"        // die Farbe ist egal, nur die Deckung zaehlt
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: sweep
        maskEnabled: true
        maskSource: frame
    }
}
