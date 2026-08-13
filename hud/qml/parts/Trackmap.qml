// Die Minimap.
// Vorlage: #trackmap in static/parts/trackmap.css, Ablauf in trackmap.js.
//
// Die Kontur kommt fertig normalisiert aus hud/extras.py - dort wird sie skaliert,
// gedreht und gespiegelt (warum in Python, steht im Kopf jener Datei). Hier wird
// nur noch gezeichnet: ein 0..100-Koordinatensystem, das auf die Kartengroesse
// skaliert wird.
//
// ⚠ Kontur und Flaggen liegen auf einem Canvas, NICHT auf QtQuick.Shapes.
// Zwei Gruende:
//   1. Die Zahl der Teilstuecke steht erst zur Laufzeit fest, und ein Repeater
//      kann keine ShapePath erzeugen - die sind keine Items. Das Ergebnis waere
//      eine leere Karte (genau der Fehler beim ersten Versuch).
//   2. Die Kontur ist statisch. Ein Canvas zeichnet sie einmal in seine Textur
//      und zeigt sie danach kostenlos - dasselbe Argument, mit dem trackmap.css
//      den Filter bewusst nur auf die statische Linie legt.
// Die Autos sind dagegen normale Items: die bewegen sich in jedem Bild.

import QtQuick
import ".."

Item {
    id: map

    readonly property var src: Kers.trackmap
    /** Umrechnung vom 0..100-System der Kontur auf Bildpunkte. */
    readonly property real unit: width / 100

    width: src.size
    height: src.size
    visible: src.isVisible

    // ── Streckenkontur ──────────────────────────────────────────────────────
    Canvas {
        id: track
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        Connections {
            target: map.src
            function onContourChanged() { track.requestPaint(); }
        }
        onWidthChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var u = map.unit;
            // Reihenfolge zaehlt: erst die dunkle Casing-Linie, dann die farbigen
            // Sektor-Teilstuecke darueber. So bekommen eng benachbarte
            // Streckenteile einen sichtbaren Trennstreifen.
            var parts = map.src.contour;
            for (var i = 0; i < parts.length; i++) {
                var p = parts[i];
                if (!p.points || p.points.length < 2)
                    continue;
                ctx.beginPath();
                ctx.moveTo(p.points[0].x * u, p.points[0].y * u);
                for (var k = 1; k < p.points.length; k++)
                    ctx.lineTo(p.points[k].x * u, p.points[k].y * u);
                ctx.strokeStyle = p.color;
                ctx.lineWidth = p.width * u;
                ctx.lineJoin = "round";
                ctx.lineCap = "round";
                ctx.stroke();
            }
        }
    }

    // ── Gelbe Flaggen ───────────────────────────────────────────────────────
    Canvas {
        id: flags
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        Connections {
            target: map.src
            function onContourChanged() { flags.requestPaint(); }
        }
        onWidthChanged: requestPaint()

        // flag-blink: die geflaggten Abschnitte pulsieren.
        SequentialAnimation on opacity {
            running: map.src.flags.length > 0
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 600; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
        }

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var u = map.unit;
            var zones = map.src.flags;
            ctx.strokeStyle = "#ffcc00";
            ctx.lineWidth = 3.4 * u;
            ctx.lineJoin = "round";
            ctx.lineCap = "round";
            for (var i = 0; i < zones.length; i++) {
                var pts = zones[i].points;
                if (!pts || pts.length < 2)
                    continue;
                ctx.beginPath();
                ctx.moveTo(pts[0].x * u, pts[0].y * u);
                for (var k = 1; k < pts.length; k++)
                    ctx.lineTo(pts[k].x * u, pts[k].y * u);
                ctx.stroke();
            }
        }
    }

    // ── Die Autos ───────────────────────────────────────────────────────────
    Repeater {
        model: map.src.cars

        Item {
            required property var modelData

            x: modelData.x * map.unit
            y: modelData.y * map.unit
            opacity: modelData.opacity
            // Die Punkte gleiten zwischen den Payloads, statt zu springen
            // (im CSS: transition: cx/cy 0.18s linear).
            Behavior on x { NumberAnimation { duration: 180 } }
            Behavior on y { NumberAnimation { duration: 180 } }

            Rectangle {
                anchors.centerIn: parent
                width: modelData.r * 2 * map.unit
                height: width
                radius: width / 2
                color: modelData.color
                border.width: (modelData.retired ? 2 : modelData.focused ? 0.7 : 0.5)
                              * map.unit
                border.color: modelData.retired ? "#ff8000"
                            : modelData.focused ? "#ffffff" : Qt.rgba(0, 0, 0, 0.6)

                // Ausgeschieden: der orange Ring blinkt.
                SequentialAnimation on border.color {
                    running: modelData.retired
                    loops: Animation.Infinite
                    ColorAnimation { to: Qt.rgba(1, 128 / 255, 0, 0.12); duration: 500 }
                    ColorAnimation { to: "#ff8000"; duration: 500 }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: modelData.label !== ""
                text: modelData.label
                color: "#ffffff"
                font { family: Theme.sans
                       pixelSize: Math.max(1, Math.round(modelData.r * 1.15 * map.unit))
                       weight: Font.Black }
                style: Text.Outline
                styleColor: Qt.rgba(0, 0, 0, 0.75)
            }
        }
    }
}
