// Der Timing Tower.
//
// Vorlage: templates/parts/tower.html, static/parts/tower.css und der Kopfteil von
// renderTower() in static/parts/tower.js. Die Spaltenbreiten unten sind die
// grid-template-columns aus dem CSS, die Farben kommen aus dem Theme.
//
// ⚠ NICHT uebernommen: der backdrop-filter (Milchglas) des Containers. Er blendet
// im Browser das UNTER dem Overlay liegende Bild weich - in einem Fenster mit
// echtem Alphakanal liegt darunter aber der Desktop bzw. in OBS gar nichts, es
// gaebe also nichts zu blenden. Im Web-Overlay war das genauso wirkungslos.

import QtQuick
import QtQuick.Effects
import ".."
import "../Fmt.js" as Fmt

Item {
    id: tower

    /** Hoehe der Buehne - Grundlage der Selbstskalierung (siehe unten). */
    property real stageHeight: 1080

    readonly property bool quali: Kers.session.isQuali

    // grid-template-columns aus tower.css:
    //   Rennen (.sectors-hidden)  36 48 169 90 90 90 – 44
    //   Quali  (.quali-mode)      36 48 151 90 78 80 116 86
    readonly property var cols: quali ? [36, 48, 151, 90, 78, 80, 116, 86]
                                      : [36, 48, 169, 90, 90, 90, 0, 44]

    readonly property real contentWidth: {
        var w = 24;                       // padding: 0 12px
        for (var i = 0; i < cols.length; i++)
            if (cols[i] > 0)
                w += cols[i] + 4;         // gap: 0 4px
        return w - 4;
    }
    // Im CSS sind 628 bzw. 732 px fest gesetzt; die Spalten fuellen davon etwas
    // weniger. Das Maximum uebernimmt beides, ohne dass etwas abgeschnitten wird.
    readonly property real panelWidth: Math.max(quali ? 732 : 628, contentWidth)

    readonly property int rowCount: Kers.drivers.count
    property int maxRowsSeen: 0
    onRowCountChanged: maxRowsSeen = Math.max(maxRowsSeen, rowCount)

    // Der Tower ist IMMER gleich hoch: die Skalierung rechnet mit dem vollen Feld,
    // nicht mit der gerade sichtbaren Fahrerzahl. Sonst wuerden die Zeilen bei
    // jedem Ausfall groesser (tower.js, FULL_GRID).
    readonly property int towerRows: Kers.settings.rows > 0
                                     ? Kers.settings.rows
                                     : Math.max(22, maxRowsSeen)

    visible: Kers.settings.showTower
    width: panelWidth * panel.scale
    height: panel.height * panel.scale

    // ── Selbstskalierung ────────────────────────────────────────────────────
    // 1:1 die Rechnung am Ende von renderTower(): der Tower soll gut 80 % der
    // Buehnenhoehe fuellen, TOWER_BASE ist die Grundvergroesserung, und
    // Kers.settings.scale ist ein Faktor RELATIV dazu (0 = automatisch).
    readonly property real towerBase: 1.12
    readonly property real autoFit: {
        var refH = headerBlock.height + colHead.height + towerRows * Theme.rowHeight;
        var availH = (stageHeight - 40) * 0.80;
        var want = refH > 0 ? availH / (refH * towerBase) : 1;
        return towerBase * Math.min(1, want);
    }

    // ── Kein Schlagschatten ─────────────────────────────────────────────────
    // Hier lag der Gegenpart zu --panel-shadow (0 12px 30px rgba(0,0,0,0.55)):
    // ein schwarzes Rechteck in Panelgroesse, weichgezeichnet per MultiEffect.
    //
    // Entfernt am 13.08.2026. Das Effekt-Item war ueber `anchors.fill` genauso
    // gross wie das Panel und ragte nur durch `topMargin: -12` heraus - nach
    // OBEN. Unten und seitlich beschnitt Qt den Weichzeichner am Item-Rand, dort
    // verschwand er unter dem Panel. Sichtbar war also allein ein dunkler
    // Streifen ueber dem Tower, und der stoerte. Ohne ihn bleibt vom Schatten
    // nichts uebrig, was man sehen koennte - der Blur kostete nur noch Rechenzeit
    // in jedem Bild.
    //
    // Falls er zurueck soll: Das Effekt-Item muss dann RINGSUM groesser sein als
    // das Panel (z.B. anchors.margins: -32 plus paddingRect), sonst wiederholt
    // sich genau dieser Fehler.

    // ── Das Panel ───────────────────────────────────────────────────────────
    Item {
        id: panel
        width: tower.panelWidth
        height: headerBlock.height + colHead.height + body.height
        transformOrigin: Item.TopLeft
        scale: tower.autoFit * (Kers.settings.scale > 0 ? Kers.settings.scale : 1)

        // Rahmen und Ecken. Der Rahmen wechselt bei Safety Car, VSC und roter
        // Flagge Farbe und Dicke - .sc-active / .vsc-active / .rf-active.
        Rectangle {
            id: frame
            anchors.fill: parent
            color: "transparent"
            radius: Theme.panelRadius
            border.width: tower.frameKind === "none" ? 1 : 2
            border.color: tower.frameColor
            z: 20

            // Safety Car und rote Flagge pulsieren; der innere Schein sitzt als
            // eigene Flaeche darunter (inset-Schatten gibt es in QML nicht).
            SequentialAnimation on border.color {
                running: tower.frameKind === "sc" || tower.frameKind === "rf"
                loops: Animation.Infinite
                ColorAnimation { to: tower.frameKind === "rf" ? "#ff8a80" : "#ffe680"
                                 duration: tower.frameKind === "rf" ? 800 : 950
                                 easing.type: Easing.InOutQuad }
                ColorAnimation { to: tower.frameKind === "rf" ? "#ff2a1a" : "#ffcc00"
                                 duration: tower.frameKind === "rf" ? 800 : 950
                                 easing.type: Easing.InOutQuad }
            }
        }

        // Innerer Schein bei SC / roter Flagge
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            visible: tower.frameKind === "sc" || tower.frameKind === "rf"
            radius: Theme.panelRadius - 2
            color: "transparent"
            border.width: 10
            border.color: tower.frameKind === "rf" ? Qt.rgba(1, 42 / 255, 26 / 255, 0.25)
                                                   : Qt.rgba(1, 196 / 255, 0, 0.28)
            z: 19
            SequentialAnimation on opacity {
                running: parent.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.0; duration: tower.frameKind === "rf" ? 800 : 950 }
                NumberAnimation { to: 0.35; duration: tower.frameKind === "rf" ? 800 : 950 }
            }
        }

        // Virtual Safety Car: Lichtpunkt laeuft am Rand entlang, kein Blinken.
        Loader {
            anchors.fill: parent
            active: tower.frameKind === "vsc"
            visible: active
            z: 25
            sourceComponent: RunningBorder {
                thickness: 4
                frameRadius: 12
                period: 4000
                stops: [{ p: 0.000, c: Qt.rgba(1, 204 / 255, 0, 0.0) },
                        { p: 0.139, c: Qt.rgba(1, 204 / 255, 0, 0.45) },
                        { p: 0.250, c: "#ffd633" },
                        { p: 0.500, c: Qt.rgba(1, 204 / 255, 0, 0.0) },
                        { p: 0.750, c: "#ffd633" }]
            }
        }

        // ── Kopfzeile ───────────────────────────────────────────────────────
        Rectangle {
            id: headerBlock
            width: parent.width
            /** Innenabstand des Kopfes - im CSS `padding: 18px 24px`. */
            readonly property int headPad: 18
            height: headerCol.implicitHeight + 2 * headPad
            topLeftRadius: Theme.panelRadius
            topRightRadius: Theme.panelRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.towerHead1 }
                GradientStop { position: 1.0; color: Theme.towerHead2 }
            }

            // border-bottom: var(--accent) solid var(--header-accent)
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.accentWidth
                color: Theme.headerAccent
            }

            Column {
                id: headerCol
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: 24; rightMargin: 24
                          topMargin: headerBlock.headPad }
                spacing: 9                                // margin: 9px 0 0 am Ticker

                // Reihenfolge wie die flex-order im CSS:
                // Logo(links, -1) · Marken-Slot(0) · Live-Punkt(1) · Titel(2) · Logo(rechts)
                Item {
                    width: parent.width
                    height: Math.max(titleRow.implicitHeight, 44)

                    // Die Titelzeile liegt UEBER dem Wetter-Ticker. Ohne das gewinnt
                    // der Ticker, weil er im Column danach kommt - und ein hohes
                    // Marken-Logo ragt ja absichtlich in ihn hinein, wurde dort aber
                    // von den Wetter-Chips ueberdeckt.
                    z: 2

                    Row {
                        id: titleRow
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 14

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: Kers.settings.towerLogo !== ""
                                     && Kers.settings.towerLogoPos !== "right"
                            source: Theme.brandLogo(Kers.settings.towerLogo)
                            height: Kers.settings.towerLogoH > 0 ? Kers.settings.towerLogoH : 34
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true             // s. Kommentar in TowerRow.qml
                            sourceSize.height: Math.max(1, Math.round(height * 2))
                            // Immer voll deckend, unabhaengig von der Panel-Deckkraft.
                            opacity: 1
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text !== ""
                            text: Kers.settings.brandTitle
                            color: Theme.accent
                            font { family: Theme.display; pixelSize: 13; weight: Font.Bold
                                   letterSpacing: 3; capitalization: Font.AllUppercase }
                        }

                        // Live-Punkt: pulsiert, solange Daten kommen.
                        Rectangle {
                            id: liveDot
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10
                            height: 10
                            radius: 5
                            color: Kers.session.connected ? Theme.accent : Theme.textMuted
                            SequentialAnimation on scale {
                                running: Kers.session.connected
                                loops: Animation.Infinite
                                NumberAnimation { from: 0.95; to: 1.0; duration: 1050 }
                                NumberAnimation { to: 0.95; duration: 450 }
                            }
                        }

                        // Session-Titel: "Q1" bzw. "Runde X / Y", dahinter das Badge.
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 14

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== ""
                                text: Kers.session.title
                                color: Theme.textMain
                                font { family: Theme.display; pixelSize: 40
                                       weight: Font.Bold; letterSpacing: 2
                                       capitalization: Font.AllUppercase }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== ""
                                text: Kers.session.subtitle
                                color: Qt.rgba(1, 1, 1, 0.9)
                                font { family: Theme.display; pixelSize: 29
                                       weight: Font.Bold; letterSpacing: 1
                                       capitalization: Font.AllUppercase }
                            }

                            // "Safety Car" / "Letzte Runde" / "Ergebnis"
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: Kers.session.flagText !== ""
                                width: flagLabel.implicitWidth + 26
                                height: flagLabel.implicitHeight + 6
                                radius: 20
                                color: Kers.session.flagKind === "sc" ? "#ffcc00" : "#15151c"
                                border.width: Kers.session.flagKind === "fin" ? 1 : 0
                                border.color: "#ffffff"

                                SequentialAnimation on opacity {
                                    running: Kers.session.flagKind === "sc"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.55; duration: 700
                                                      easing.type: Easing.InOutQuad }
                                    NumberAnimation { to: 1.0; duration: 700
                                                      easing.type: Easing.InOutQuad }
                                }

                                Text {
                                    id: flagLabel
                                    anchors.centerIn: parent
                                    text: Kers.session.flagText
                                    color: Kers.session.flagKind === "sc" ? "#2a1f00" : "#ffffff"
                                    font { family: Theme.display; pixelSize: 22
                                           weight: Font.Bold; letterSpacing: 2
                                           capitalization: Font.AllUppercase }
                                }
                            }
                        }
                    }

                    Image {
                        id: brandRight
                        anchors.right: parent.right
                        // Weiter nach rechts, ueber den 24-px-Innenabstand des Kopfes
                        // hinweg. Bei -24 stand es exakt buendig an der Panelkante,
                        // jetzt sitzt es 6 px weiter innen.
                        // Kleiner = weiter rechts, groesser = weiter rein.
                        anchors.rightMargin: -18
                        visible: Kers.settings.towerLogo !== ""
                                 && Kers.settings.towerLogoPos === "right"
                        source: Theme.brandLogo(Kers.settings.towerLogo)
                        height: Kers.settings.towerLogoH > 0 ? Kers.settings.towerLogoH : 34
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true                 // s. Kommentar in TowerRow.qml
                        sourceSize.height: Math.max(1, Math.round(height * 2))

                        // Das Logo bleibt IMMER voll deckend. Die Deckkraft aus den
                        // Settings faerbt nur die Panelflaechen ein (Theme.uiAlpha);
                        // hier steht es ausdruecklich, damit es dabei bleibt.
                        opacity: 1

                        // ⚠ Nicht schlicht vertikal zentriert: das Logo ist mit 90 px
                        // deutlich hoeher als die Titelzeile (44 px) und ragt oben und
                        // unten heraus. Nach UNTEN in den Wetter-Ticker ist das
                        // gewollt, nach OBEN klebte es am Panelrand und wurde
                        // angeschnitten. Deshalb: mittig, aber nie hoeher als
                        // TOP_GAP unter der Oberkante des Kopfes.
                        readonly property int topGap: 10
                        y: Math.max(topGap - headerBlock.headPad,
                                    (parent.height - height) / 2)
                    }
                }

                // ── Wetter-Ticker ───────────────────────────────────────────
                Item {
                    width: parent.width
                    height: visible ? 74 : 0
                    visible: Kers.settings.showTicker

                    Row {
                        id: chips
                        anchors.centerIn: parent
                        spacing: 0
                        opacity: 1
                        // Wechselt der Inhalt (z.B. +10 Min -> +5 Min), blendet er
                        // weich um - gleicher Effekt wie rotateTicker() im Web.
                        Behavior on opacity { NumberAnimation { duration: 500 } }

                        Repeater {
                            model: Kers.session.ticker
                            Rectangle {
                                required property var modelData
                                width: Math.max(98, chipCol.implicitWidth + 24)
                                height: chipCol.implicitHeight + 12
                                radius: 8
                                color: modelData.now ? Qt.rgba(1, 1, 1, 0.11)
                                                     : Qt.rgba(1, 1, 1, 0.06)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.14)

                                Column {
                                    id: chipCol
                                    anchors.centerIn: parent
                                    spacing: 3

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.label
                                        color: "#ffffff"
                                        font { family: Theme.sans; pixelSize: 14
                                               weight: Font.Bold; letterSpacing: 0.5 }
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.emoji
                                        font.pixelSize: 22
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData.rain + "%"
                                        color: "#4db8ff"
                                        font { family: Theme.sans; pixelSize: 15
                                               weight: Font.DemiBold }
                                    }
                                }
                            }
                        }
                    }

                    // Kein Wetter im Payload -> derselbe Ersatztext wie im Web.
                    Text {
                        anchors.centerIn: parent
                        visible: Kers.session.ticker.length === 0
                        text: "Race Telemetry"
                        color: "#ffffff"
                        font { family: Theme.display; pixelSize: 26; letterSpacing: 1 }
                    }
                }
            }
        }

        // ── Spaltenkopf ─────────────────────────────────────────────────────
        Rectangle {
            id: colHead
            anchors.top: headerBlock.bottom
            width: parent.width
            height: 36
            color: Theme.headerRowBg

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Qt.rgba(1, 1, 1, 0.1)
            }

            Row {
                anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                spacing: 4

                Repeater {
                    model: [
                        { t: "POS", a: Text.AlignLeft },
                        { t: "", a: Text.AlignLeft },
                        { t: "DRIVER", a: Text.AlignLeft },
                        { t: Kers.session.headLeader, a: Text.AlignRight },
                        { t: Kers.session.headInterval, a: Text.AlignRight },
                        { t: "TYRE", a: Text.AlignLeft },
                        { t: "SECTORS", a: Text.AlignLeft },
                        { t: Kers.session.headRight, a: Text.AlignHCenter }
                    ]
                    Item {
                        required property int index
                        required property var modelData
                        visible: tower.cols[index] > 0
                        width: tower.cols[index]
                        height: parent.height
                        Text {
                            anchors.fill: parent
                            text: modelData.t
                            color: Theme.textMuted
                            horizontalAlignment: modelData.a
                            verticalAlignment: Text.AlignVCenter
                            font { family: Theme.sans; pixelSize: 11; weight: Font.DemiBold
                                   letterSpacing: 1; capitalization: Font.AllUppercase }
                        }
                    }
                }
            }
        }

        // ── Fahrerzeilen ────────────────────────────────────────────────────
        Item {
            id: body
            anchors.top: colHead.bottom
            width: parent.width
            // Nur so hoch wie tatsaechlich Fahrer da sind - kein leeres Feld unten.
            // Die ZEILENGROESSE bleibt trotzdem konstant, die kommt aus der
            // Skalierung oben (Referenz: towerRows).
            height: tower.rowCount * Theme.rowHeight

            Repeater {
                model: Kers.drivers
                delegate: TowerRow {
                    width: body.width
                    cols: tower.cols
                    quali: tower.quali
                    poleTime: Kers.session.poleTime
                    lastSlot: tower.rowCount - 1
                }
            }

            // Elimination-Linie (Quali)
            Item {
                visible: Kers.session.elimCut > 0
                         && Kers.session.elimCut < tower.rowCount
                y: Kers.session.elimCut * Theme.rowHeight
                width: parent.width
                height: 0
                z: 60

                Rectangle {
                    anchors { left: parent.left; right: parent.right
                              leftMargin: 8; rightMargin: 8 }
                    y: -1
                    height: 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.00; color: "transparent" }
                        GradientStop { position: 0.18; color: "#ff2a1a" }
                        GradientStop { position: 0.82; color: "#ff2a1a" }
                        GradientStop { position: 1.00; color: "transparent" }
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: -height / 2
                    width: elimLabel.implicitWidth + 32
                    height: elimLabel.implicitHeight + 4
                    radius: 11
                    color: "#15151c"
                    border.width: 1
                    border.color: Qt.rgba(1, 60 / 255, 45 / 255, 0.85)
                    Text {
                        id: elimLabel
                        anchors.centerIn: parent
                        text: "ELIMINATION"
                        color: "#ff6b6b"
                        font { family: Theme.display; pixelSize: 15; weight: Font.Bold
                               letterSpacing: 4 }
                    }
                }
            }
        }

        // ── "Warte auf Telemetrie" ──────────────────────────────────────────
        // Nur wenn NOCH NIE Daten kamen oder das letzte Ergebnis abgelaufen ist -
        // direkt nach einer Session bleiben die Standings stehen (CFG.holds).
        Rectangle {
            anchors.fill: parent
            visible: !Kers.session.connected && !Kers.session.holdResult
            color: Qt.rgba(12 / 255, 12 / 255, 16 / 255, 0.93)
            radius: Theme.panelRadius
            z: 60

            Column {
                anchors.centerIn: parent
                spacing: 14

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 16
                    height: 16
                    radius: 8
                    color: "#ffcc00"
                    SequentialAnimation on opacity {
                        running: parent.parent.parent.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 800 }
                        NumberAnimation { to: 1.0; duration: 800 }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "WARTE AUF TELEMETRIE…"
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 30; weight: Font.Bold
                           letterSpacing: 3 }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Kein Kontakt zum Server ist etwas anderes als kein Spiel -
                    // im Web-Overlay sah man den Unterschied nur in der Konsole.
                    text: Kers.session.linked
                          ? "F1 starten · UDP-Format „2026“ · Port 20777"
                          : "Kein Kontakt zum Server – läuft main.py?"
                    color: "#9a9aa8"
                    font { family: Theme.sans; pixelSize: 14 }
                }
            }
        }
    }

    // Welcher Rahmen gilt? Rote Flagge schlaegt Safety Car.
    readonly property string frameKind: {
        if (Kers.session.redFlag) return "rf";
        if (Kers.session.scStatus === "sc") return "sc";
        if (Kers.session.scStatus === "vsc") return "vsc";
        return "none";
    }

    readonly property color frameColor: {
        switch (frameKind) {
        case "rf": return "#ff2a1a";
        case "sc": return "#ffcc00";
        case "vsc": return Qt.rgba(1, 204 / 255, 0, 0.22);
        }
        return Theme.panelBorder;
    }
}
