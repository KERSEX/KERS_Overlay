// Podiumszahl mit Metallverlauf und Glanz-Sweep.
//
// Gegenstueck zu .rank-1/.rank-2/.rank-3 in static/parts/tower.css. Dort macht
// das `background-clip: text` mit zwei Ebenen: unten der Metallverlauf (180deg),
// darueber ein schmaler heller Streifen (115deg), der per @keyframes medal-glint
// alle 3,5 s einmal ueber die Zahl wandert.
//
// In QML gibt es keine Verlaufsfuellung fuer Text. Der Weg hier ist derselbe wie
// im Browser, nur andersherum aufgeschrieben: die beiden Verlaufsebenen werden
// als Rechtecke gemalt und die ZAHL dient als Maske darueber.
//
// Nur P1-P3 nutzen das. Alle anderen Zeilen bekommen schlichten Text, damit nicht
// 22 Delegates je eine eigene Ebene in den Szenengraphen haengen.

import QtQuick
import QtQuick.Effects
import "Fmt.js" as Fmt

Item {
    id: medal

    /** Wie fein die beiden Ebenen gerastert werden, bevor sie gestaucht werden.
     *
     *  ⚠ Der Grund: der Tower wird an die Bildhoehe eingepasst und dabei fast
     *  immer verkleinert - bei voller Fahrerliste auf 0,717. P4 und tiefer sind
     *  echter Text und werden dabei sauber neu gerastert. P1 bis P3 nicht: die
     *  Ziffer wird hier zur Maske fuer einen Metallverlauf, und dafuer muessen
     *  Verlauf und Maske erst in eine Textur gezeichnet werden. Eine Textur wird
     *  beim Stauchen ABGETASTET, nicht neu gezeichnet - deshalb wirkten die drei
     *  Podiumszahlen matschiger und dadurch kleiner als die darunter.
     *
     *  Mit der dreifachen Texturgroesse wird ueberabgetastet: gezeichnet wird
     *  gross, verkleinert wird danach. Kostet bei einer 27-px-Ziffer in drei
     *  Zeilen praktisch nichts.
     */
    readonly property int schaerfe: 3

    property string text: ""
    property int rank: 1
    property alias font: mask.font
    /** Laeuft der Glanz? Im Standbild (Ergebnis eingefroren) waere er unpassend. */
    property bool glinting: true

    implicitWidth: mask.implicitWidth
    implicitHeight: mask.implicitHeight

    // ── Ebene 1+2: Metall und Glanz ─────────────────────────────────────────
    Item {
        id: paint
        anchors.fill: parent
        visible: false
        clip: true                 // der Glanzstreifen ragt sonst neben die Ziffer
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.ceil(width * medal.schaerfe),
                                   Math.ceil(height * medal.schaerfe))

        // Der 180deg-Verlauf aus tower.css (.rank-1/-2/-3, zweite Ebene).
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.00; color: medal.stops[0] }
                GradientStop { position: 0.45; color: medal.stops[1] }
                GradientStop { position: 0.72; color: medal.stops[2] }
                GradientStop { position: 1.00; color: medal.stops[3] }
            }
        }

        // Der Glanzstreifen. Im CSS ein 115deg-Verlauf, hier ein schmales,
        // gekipptes Rechteck, das quer ueber die Zahl faehrt - sichtbar ist davon
        // ohnehin nur, was die Maske durchlaesst.
        Rectangle {
            id: shine
            width: parent.width * 0.55
            height: parent.height * 2.4
            y: -parent.height * 0.7
            rotation: -25
            transformOrigin: Item.Center
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: medal.rank === 3
                                                     ? "#fff8eb" : "#ffffff" }
                GradientStop { position: 1.0; color: "transparent" }
            }

            // medal-glint: schneller Durchlauf, dann Pause - die Pause steckt im
            // Original in den Keyframe-Positionen (35 % Weg, 65 % Stillstand).
            SequentialAnimation on x {
                running: medal.glinting && medal.visible
                loops: Animation.Infinite
                NumberAnimation {
                    from: -shine.width
                    to: paint.width + shine.width
                    duration: 1225                      // 35 % von 3,5 s
                    easing.type: Easing.InOutQuad
                }
                PauseAnimation { duration: 2275 }       // der Rest der 3,5 s
            }
        }
    }

    // ── Maske: die Zahl selbst ──────────────────────────────────────────────
    Text {
        id: mask
        anchors.fill: parent
        text: medal.text
        visible: false
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.ceil(width * medal.schaerfe),
                                   Math.ceil(height * medal.schaerfe))
        verticalAlignment: Text.AlignVCenter
    }

    // Tiefe. Im CSS steht dafuer `filter: drop-shadow(0 1px 1.5px ...)` und
    // ausdruecklich KEIN text-shadow - der wuerde die durchsichtige Fuellung
    // ueberlagern und das Metall abdunkeln.
    //
    // ⚠ Auch NICHT ueber shadowEnabled im MultiEffect unten: zusammen mit
    // maskEnabled kippt dort die Maske und man sieht statt der Ziffer den vollen
    // Verlaufskasten mit der Ziffer als Loch darin. Deshalb hier von Hand - eine
    // zweite, dunkle Kopie der Ziffer, um einen Punkt nach unten versetzt.
    Text {
        anchors.fill: parent
        anchors.topMargin: 1
        text: medal.text
        font: mask.font
        color: Qt.rgba(0, 0, 0, 0.55)
        verticalAlignment: Text.AlignVCenter
        z: -1
    }

    MultiEffect {
        anchors.fill: parent
        source: paint
        maskEnabled: true
        maskSource: mask
    }

    /** Die vier Stopps des Metallverlaufs - stehen in Fmt.js, damit sie nur an
     *  einer Stelle gepflegt werden muessen. */
    readonly property var stops: Fmt.medalStops(rank)
}
