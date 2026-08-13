// Ein PNG als Form, eingefaerbt in einer beliebigen Farbe.
//
// Im CSS machen das die Damage-Icons per `-webkit-mask: url(...)` plus
// `background-color` (siehe .dw-fwl/.dw-fwr/.dw-rw in static/parts/tower.css):
// das Bild liefert nur den Umriss, die Farbe kommt von aussen und zeigt die
// Schwere des Schadens.
//
// In QML dasselbe Prinzip: eine Flaeche in der Zielfarbe, maskiert mit dem Bild.

import QtQuick
import QtQuick.Effects

Item {
    id: icon

    property url source
    property color color: "#ffffff"
    /** Schwerer Schaden pulsiert - Gegenstueck zu .dmg-wing.crit. */
    property bool blinking: false

    Rectangle {
        id: fill
        anchors.fill: parent
        color: icon.color
        visible: false
        layer.enabled: true
    }

    Image {
        id: shape
        anchors.fill: parent
        source: icon.source
        fillMode: Image.PreserveAspectFit
        // Ohne das rechnet Qt das PNG in Originalgroesse und skaliert erst danach -
        // bei 18 px breiten Icons sichtbar unscharf.
        sourceSize.width: Math.max(1, Math.round(icon.width * 2))
        sourceSize.height: Math.max(1, Math.round(icon.height * 2))
        visible: false
        layer.enabled: true
        smooth: true
    }

    // Der Schlagschatten aus dem CSS (drop-shadow(0 1px 1px rgba(0,0,0,0.7))):
    // dieselbe Form, schwarz, einen Punkt tiefer.
    // ⚠ NICHT ueber shadowEnabled im MultiEffect: zusammen mit maskEnabled kippt
    // dort die Maske und man sieht das volle Rechteck statt der Fluegelform.
    Rectangle {
        id: shadowFill
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: false
        layer.enabled: true
    }
    MultiEffect {
        anchors.fill: parent
        anchors.topMargin: 1
        source: shadowFill
        maskEnabled: true
        maskSource: shape       // dieselbe Maskentextur wie die Fuellung oben
        z: -1
    }

    MultiEffect {
        anchors.fill: parent
        source: fill
        maskEnabled: true
        maskSource: shape

        SequentialAnimation on opacity {
            running: icon.blinking
            loops: Animation.Infinite
            NumberAnimation { to: 0.5; duration: 575; easing.type: Easing.InOutQuad }
            NumberAnimation { to: 1.0; duration: 575; easing.type: Easing.InOutQuad }
        }
    }
}
