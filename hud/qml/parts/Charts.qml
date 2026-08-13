// Verlaufs-Charts: Positionen, Gap oder Rundenzeiten.
// Vorlage: #poschart in static/parts/charts.css, Ablauf in charts.js.
//
// Eingeschaltet wird das Chart in /regie. Python liefert die Reihen als ROHWERTE
// (Runde + Wert), die Umrechnung in Bildpunkte passiert hier - sie haengt an der
// Panelgroesse und muss sich beim Skalieren mitbewegen.

// ⚠ Die Linien liegen auf einem Canvas und nicht auf QtQuick.Shapes: die Zahl der
// Fahrer steht erst zur Laufzeit fest, und ein Repeater kann keine ShapePath
// erzeugen (das sind keine Items). Neu gezeichnet wird nur, wenn Python neue
// Reihen liefert - also alle drei Sekunden, nicht pro Bild.

import QtQuick
import ".."

Item {
    id: charts

    readonly property var src: Kers.charts

    visible: src.isVisible

    // Zeichenflaeche innerhalb des Panels (padL/padR/padT/padB in charts.js)
    readonly property real padL: 48
    readonly property real padR: 150
    readonly property real padT: 20
    readonly property real padB: 34

    // Der abgedunkelte Grund ueber dem ganzen Bild. Im Gesamt-Overlay gehoert er
    // dazu (#poschart hat inset: 0 und rgba(6,6,9,0.85)); nur die EINZELSEITE
    // /part/charts laesst ihn weg, weil sie als eigene OBS-Quelle nicht den halben
    // Bildschirm verdunkeln soll. Hier ist das Gesamt-Overlay gemeint.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(6 / 255, 6 / 255, 9 / 255, 0.85)
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(1180, parent.width * 0.9)
        height: parent.height * 0.82
        color: Theme.panelBg
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        radius: 14
        clip: true

        // ── Kopf ────────────────────────────────────────────────────────────
        Rectangle {
            id: head
            width: parent.width
            height: 58
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.towerHead1 }
                GradientStop { position: 1.0; color: Theme.towerHead2 }
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.accentWidth
                color: Theme.accent
            }

            Text {
                anchors { left: parent.left; leftMargin: 24
                          verticalCenter: parent.verticalCenter }
                text: charts.src.title
                color: Theme.textMain
                font { family: Theme.display; pixelSize: 30; weight: Font.Bold
                       letterSpacing: 3; capitalization: Font.AllUppercase }
            }
            Text {
                anchors { right: parent.right; rightMargin: 24
                          verticalCenter: parent.verticalCenter }
                text: "Steuerung über /regie"
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 12 }
            }
        }

        // ── Zeichenflaeche ──────────────────────────────────────────────────
        Item {
            id: plot
            anchors { top: head.bottom; left: parent.left; right: parent.right
                      bottom: parent.bottom; margins: 16 }

            readonly property real w: width
            readonly property real h: height
            readonly property int maxLap: charts.src.maxLap
            readonly property real maxY: charts.src.maxValue
            readonly property bool zero: charts.src.zeroBased

            function px(lap) {
                return charts.padL + (maxLap <= 1 ? 0 : (lap - 1) / (maxLap - 1))
                       * (w - charts.padL - charts.padR);
            }
            function py(v) {
                if (zero)
                    return charts.padT + (maxY <= 0 ? 0 : v / maxY)
                           * (h - charts.padT - charts.padB);
                return charts.padT + (maxY <= 1 ? 0 : (v - 1) / (maxY - 1))
                       * (h - charts.padT - charts.padB);
            }

            Text {
                anchors { left: parent.left; top: parent.top; margins: 6 }
                visible: charts.src.emptyText !== ""
                text: charts.src.emptyText
                color: Theme.textMuted
                font { family: Theme.sans; pixelSize: 11 }
            }

            // Waagerechtes Gitter mit Beschriftung
            Repeater {
                model: plot.zero ? 6 : Math.max(0, Math.min(24, Math.ceil(plot.maxY)))
                Item {
                    required property int index
                    readonly property real value: plot.zero
                        ? (plot.maxY / 5) * index : index + 1
                    visible: plot.maxLap > 0 && (plot.zero || value <= plot.maxY)

                    Rectangle {
                        x: charts.padL
                        y: plot.py(parent.value)
                        width: plot.w - charts.padL - charts.padR
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }
                    Text {
                        x: charts.padL - 8 - width
                        y: plot.py(parent.value) - height / 2
                        text: plot.zero ? "+" + parent.value.toFixed(0) + "s"
                                        : "P" + parent.value
                        color: Theme.textMuted
                        font { family: Theme.sans; pixelSize: 11 }
                    }
                }
            }

            // Rundennummern an der Unterkante
            Repeater {
                model: plot.maxLap > 0
                       ? Math.floor((plot.maxLap - 1) / Math.max(1, Math.round(plot.maxLap / 12))) + 1
                       : 0
                Text {
                    required property int index
                    readonly property int lap: 1 + index * Math.max(1, Math.round(plot.maxLap / 12))
                    x: plot.px(lap) - width / 2
                    y: plot.h - 24
                    text: lap
                    color: Theme.textMuted
                    font { family: Theme.sans; pixelSize: 11 }
                }
            }

            // Eine Linie je Fahrer
            Canvas {
                id: lines
                anchors.fill: parent
                renderStrategy: Canvas.Cooperative

                Connections {
                    target: charts.src
                    function onSeriesChanged() { lines.requestPaint(); }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.lineWidth = 2.5;
                    ctx.lineJoin = "round";
                    ctx.lineCap = "round";
                    ctx.globalAlpha = 0.9;
                    var series = charts.src.series;
                    for (var i = 0; i < series.length; i++) {
                        var pts = series[i].points;
                        if (!pts || pts.length < 1)
                            continue;
                        ctx.beginPath();
                        ctx.moveTo(plot.px(pts[0].lap), plot.py(pts[0].value));
                        for (var k = 1; k < pts.length; k++)
                            ctx.lineTo(plot.px(pts[k].lap), plot.py(pts[k].value));
                        ctx.strokeStyle = series[i].color;
                        ctx.stroke();
                    }
                }
            }

            // Endpunkt und Name rechts an der Linie
            Repeater {
                model: charts.src.series
                Item {
                    required property var modelData
                    readonly property var lastPoint:
                        modelData.points[modelData.points.length - 1]
                    x: plot.px(lastPoint.lap)
                    y: plot.py(lastPoint.value)

                    Rectangle {
                        anchors.centerIn: parent
                        width: 7
                        height: 7
                        radius: 3.5
                        color: modelData.color
                    }
                    Text {
                        x: 8
                        y: -height / 2
                        text: modelData.name
                        color: modelData.color
                        font { family: Theme.sans; pixelSize: 11; weight: Font.Bold }
                    }
                }
            }
        }
    }
}
