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

    /** Mit welchem Faktor diese Ziffer am Ende auf dem Schirm landet.
     *
     *  Der Tower wird als Ganzes gestaucht - bei voller Fahrerliste auf 0,717,
     *  dazu kommen der Regler "Skalierung" und der Groessenfaktor aus dem freien
     *  Layout. P4 und tiefer sind schlichter Text: den rastert Qt in der
     *  Endgroesse. P1 bis P3 nicht - dort wird die Ziffer zur Maske fuer einen
     *  Metallverlauf, und dafuer muessen Verlauf und Maske erst in je eine
     *  Textur. Eine Textur wird beim Stauchen ABGETASTET, nicht neu gezeichnet.
     *
     *  Deshalb wird die Textur gleich in der ENDGROESSE angelegt: dann zeichnet
     *  Qt die Ziffer genau so gross, wie sie zu sehen ist, und es bleibt nichts
     *  zu verkleinern. Wird der Faktor nicht gesetzt, gilt 1 - dann ist es die
     *  Ausgangsgroesse wie vor 0.2.2.
     *
     *  ⚠ Hier hat 0.2.2 danebengegriffen: dreifache Texturgroesse, in der
     *  Annahme "gross zeichnen, klein anzeigen sei besser". Das Gegenteil ist der
     *  Fall - die Verkleinerung war damit 3/0,717 = 4,2:1 statt 1,4:1, und die
     *  bilineare Filterung liest nur 2x2 Texel. Aus der 2 und der 3 wurden
     *  Klumpen; die 1 ueberstand es, weil ein senkrechter Balken in der Hoehe
     *  nichts zu verlieren hat. Genau die Falle, die bei den Team-Logos schon
     *  einmal zugeschlagen hat - siehe den mipmap-Kommentar in parts/TowerRow.qml.
     */
    property real bildFaktor: 1

    /** Texturgroesse in echten Bildpunkten. devicePixelRatio muss hier von Hand
     *  hinein: sobald layer.textureSize gesetzt ist, rechnet Qt ihn nicht mehr
     *  selbst dazu, und auf einem skalierten Bildschirm waere die Ziffer sonst
     *  halb so fein wie alles andere. */
    readonly property real texFaktor: Math.max(0.25, bildFaktor * Screen.devicePixelRatio)
    function texGroesse(w, h) {
        return Qt.size(Math.max(1, Math.ceil(w * medal.texFaktor)),
                       Math.max(1, Math.ceil(h * medal.texFaktor)));
    }

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
        // ⚠ KEIN layer.mipmap. Als "Auffangnetz" gedacht, war es vermutlich Teil
        // des Problems: die Mipmap-Stufe waehlt die GPU aus den Ableitungen der
        // Texturkoordinaten, und greift sie bei einer 20x20-Maske eine Stufe zu
        // hoch, sind das 10x10 - dann ist die Ziffer Matsch. Auf einer MASKE
        // kostet ein falscher Grad die Form, nicht nur Schaerfe. Bei den
        // Team-Logos ist mipmap richtig, dort wird wirklich stark verkleinert.
        layer.textureSize: medal.texGroesse(width, height)

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
        layer.textureSize: medal.texGroesse(width, height)
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
