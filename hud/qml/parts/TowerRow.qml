// Eine Fahrerzeile im Timing Tower.
//
// Vorlage: .driver-row samt Unterelementen in static/parts/tower.css und der
// Zeilenteil von renderTower() in static/parts/tower.js. Die Werte kommen fertig
// aus hud/models.py - hier wird nur noch dargestellt und animiert.
//
// ⚠ Die Zeile positioniert sich SELBST ueber `y: slot * rowHeight`. Genau wie im
// Web-Overlay, wo die Zeilen absolut sitzen und ein transform sie verschiebt:
// dadurch gleitet P5 sichtbar an P4 vorbei, statt dass zwei stehende Zeilen ihren
// Inhalt tauschen. Siehe auch den Kommentar zu `slot` in hud/models.py.
//
// ⚠ Schriftgroessen sind ganzzahlig. Das CSS hat an drei Stellen halbe Pixel
// (13.5 / 14.5 / 0.72em von 40 = 28.8); font.pixelSize ist in Qt aber ein int.
// Gerundet auf 14 / 15 / 29 - bei der Tower-Skalierung von 0,56 bis 1,12 liegt der
// Unterschied deutlich unter einem Bildpunkt.

import QtQuick
import QtQuick.Effects
import ".."
import "../Fmt.js" as Fmt

Item {
    id: row

    // ── Von der Tabelle gesetzt ─────────────────────────────────────────────
    property var cols: []              // Spaltenbreiten, siehe Tower.qml
    property bool quali: false
    property real poleTime: 0
    property int lastSlot: 0           // unterste Zeile rundet die Panelecken ab

    // ── Aus dem Modell ──────────────────────────────────────────────────────
    required property int slot
    required property int position
    required property string name
    required property string team
    required property color teamColor
    required property string teamLogo
    required property bool even
    required property bool isFocused
    required property bool isFinished
    required property bool isFastestLap
    required property bool elimZone
    required property bool lapInvalid
    required property string changeDir
    required property int changeStamp
    required property string flashKind
    required property int flashStamp
    required property real gapToLeader
    required property real gapToAhead
    required property real bestLap
    required property int lapsDown
    required property bool dnf
    required property bool dsq
    required property bool inPit
    required property string compound
    required property int tyreAge
    required property string tyreIcon
    required property int tyreStamp
    required property var sectors
    required property var sectorClasses
    required property string qualiStatus
    required property bool drs
    required property bool overtakeActive
    required property bool overtakeAvailable
    required property int penalties
    required property int penDt
    required property int cornerWarnings
    required property int comeback
    required property int dmgFl
    required property int dmgFr
    required property int dmgRw

    readonly property bool isLeader: position === 1 && !quali && !dnf && !dsq
    readonly property bool podium: position >= 1 && position <= 3

    height: Theme.rowHeight
    y: slot * Theme.rowHeight

    // Sanftes Gleiten beim Positionswechsel. Kurve und Dauer wie in tower.css
    // (transform 0.6s cubic-bezier(0.65, 0, 0.35, 1)).
    Behavior on y {
        enabled: row.settled
        NumberAnimation {
            duration: 600
            easing.type: Easing.Bezier
            easing.bezierCurve: [0.65, 0, 0.35, 1, 1, 1]
        }
    }

    // Neue Zeilen blenden an ihrem Platz ein, statt aus der Ecke heranzugleiten -
    // deshalb die Animation erst nach dem ersten Bild scharfschalten.
    property bool settled: false
    opacity: 0
    Component.onCompleted: {
        opacity = 1;
        settled = true;
    }
    Behavior on opacity { NumberAnimation { duration: 450 } }

    // ── Zeilenhintergrund ───────────────────────────────────────────────────
    Rectangle {
        id: bg
        anchors.fill: parent
        // ⚠ Ein Pixel Ueberstand nach unten (negativer Margin), ausser bei der
        // letzten Zeile. Zeilenhoehe (62) und y sind zwar ganzzahlig, der Tower
        // wird aber als Ganzes skaliert (panel.scale, dazu der Regler in
        // /settings). Dadurch liegen die Zeilenkanten auf gebrochenen Pixeln, und
        // zwischen zwei Zeilen blieb stellenweise eine haarfeine Luecke, durch die
        // das Spiel durchschien - zuletzt zwischen P19 und P20. Die naechste Zeile
        // deckt den Ueberstand wieder zu, bei der letzten bleibt die runde Ecke heil.
        anchors.bottomMargin: row.slot === row.lastSlot ? 0 : -1
        color: row.elimZone ? Qt.rgba(225 / 255, 6 / 255, 0, 0.22)
                            : (row.even ? Theme.rowBg : Theme.rowBgAlt)
        // Nur die unterste Zeile rundet unten ab - Gegenstueck zu .last-row.
        bottomLeftRadius: row.slot === row.lastSlot ? Theme.panelRadius : 0
        bottomRightRadius: row.slot === row.lastSlot ? Theme.panelRadius : 0
        Behavior on color { ColorAnimation { duration: 500 } }

        Rectangle {          // border-bottom: 1px rgba(255,255,255,0.03)
            // Der bottomMargin gleicht den Ueberstand oben wieder aus, sonst laege
            // die Trennlinie unter der naechsten Zeile und waere unsichtbar.
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                      bottomMargin: row.slot === row.lastSlot ? 0 : 1 }
            height: 1
            color: Qt.rgba(1, 1, 1, 0.03)
            visible: row.slot !== row.lastSlot
        }
    }

    // Bestrunde-Flash: gruen bei neuer persoenlicher, lila bei neuer Session-Bestzeit.
    // Im CSS ein einmaliger inset-Glow (0 -> Peak -> 0), hier eine kurz aufleuchtende
    // Flaeche - dasselbe Verhalten, ohne inset-Schatten nachbauen zu muessen.
    Rectangle {
        id: pbFlash
        anchors.fill: parent
        color: row.flashKind === "p" ? "#b14bff" : "#00e676"
        opacity: 0
        SequentialAnimation {
            id: pbAnim
            NumberAnimation { target: pbFlash; property: "opacity"; to: 0.26; duration: 560 }
            NumberAnimation { target: pbFlash; property: "opacity"; to: 0.0; duration: 1040 }
        }
    }
    onFlashStampChanged: if (flashStamp > 0) pbAnim.restart()

    // Aktuell betrachtetes Auto ("auf wen bin ich drauf") -> weisse Umrandung.
    Rectangle {
        anchors.fill: parent
        visible: row.isFocused
        color: "transparent"
        radius: 5
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, 0.92)
        z: 6
    }

    // ── Strafen-Pillen: haengen seitlich aus der Zeile heraus ───────────────
    //
    // Seite kommt aus den Settings. ⚠ Rechts steht auch die Zielflagge (gleich
    // unten, `left: parent.right`) - beide wollen denselben Platz. Deshalb:
    //   penHideFinish an  -> die Pillen verschwinden, sobald die Flagge steht
    //   penHideFinish aus -> sie ruecken hinter die Flagge (27 px = deren
    //                        Breite 22 plus ihr Abstand 5)
    // Links gibt es den Konflikt nicht, dort bleibt der Abstand wie gehabt.
    Column {
        id: penStack

        readonly property bool rechts: Kers.settings.penSide === "right"
        readonly property bool zielflagge: row.isFinished
        visible: !(rechts && zielflagge && Kers.settings.penHideFinish)

        // ⚠ Bewusst x statt umschaltbarer Anker: ein Anker laesst sich mit
        // `undefined` NICHT verlaesslich wieder abschalten - dieselbe Falle, die
        // beim Trackmap-Umbau die Karte gleichzeitig links UND rechts haengen
        // liess (siehe Overlay.qml). verticalCenter bleibt ein Anker, der wird
        // ja nie umgeschaltet.
        anchors.verticalCenter: parent.verticalCenter
        x: rechts ? row.width + (zielflagge ? 5 + 27 : 5)
                  : -width - 8
        spacing: 3
        z: 5

        // Durchfahrtstrafe hat Vorrang vor der Zeitstrafe.
        Rectangle {
            visible: !row.dsq && (row.penDt > 0 || row.penalties > 0)
            // Buendig zur Zeile: links haengend nach rechts, rechts haengend
            // nach links. Wieder x statt Anker, gleiche Begruendung wie oben.
            x: penStack.rechts ? 0 : penStack.width - width
            width: penLabel.implicitWidth + 14
            height: penLabel.implicitHeight + 8
            radius: 4
            color: row.penDt > 0 ? "#ff8000" : Theme.accent
            Text {
                id: penLabel
                anchors.centerIn: parent
                text: row.penDt > 0 ? "DT" : "+" + row.penalties + "s"
                color: "#ffffff"
                font { family: Theme.display; pixelSize: 16; weight: Font.Bold
                       letterSpacing: 0.5 }
            }
        }

        // Track-Limits: 1. Verwarnung gelb, 2. orange. Bei der 3. gibt es die Strafe,
        // der Zaehler beginnt von vorn (das Modulo macht schon models.py).
        Rectangle {
            visible: !row.dsq && (row.cornerWarnings === 1 || row.cornerWarnings === 2)
            x: penStack.rechts ? 0 : penStack.width - width
            width: tlLabel.implicitWidth + 12
            height: tlLabel.implicitHeight + 6
            radius: 4
            color: row.cornerWarnings === 1 ? "#ffd000" : "#ff8000"
            Text {
                id: tlLabel
                anchors.centerIn: parent
                text: row.cornerWarnings
                color: row.cornerWarnings === 1 ? "#1a1a1a" : "#ffffff"
                font { family: Theme.display; pixelSize: 13; weight: Font.Bold
                       letterSpacing: 0.5 }
            }
        }
    }

    // ── Zielflagge: klebt rechts am Tower-Rand ──────────────────────────────
    Item {
        visible: row.isFinished
        anchors { left: parent.right; leftMargin: 5; verticalCenter: parent.verticalCenter }
        width: 22
        height: 15
        z: 4
        Rectangle {
            anchors.fill: parent
            color: "#f4f4f6"
            radius: 2
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.65)
            clip: true
            // Echtes Schachbrett aus 3,75-px-Feldern, wie die beiden 45deg-Verlaeufe
            // in .finish-flag.
            Grid {
                columns: 6
                rows: 4
                Repeater {
                    model: 24
                    Rectangle {
                        width: 3.75
                        height: 3.75
                        color: ((index % 6) + Math.floor(index / 6)) % 2 === 0
                               ? "#14141a" : "transparent"
                    }
                }
            }
        }
    }

    // ── Der eigentliche Zeileninhalt ────────────────────────────────────────
    Row {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
        spacing: 4

        // 1 · Position + Auf-/Ab-Pfeil
        Item {
            width: row.cols[0]
            height: parent.height

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Item {
                    width: rankPlain.implicitWidth
                    height: rankPlain.implicitHeight
                    anchors.verticalCenter: parent.verticalCenter
                    scale: 1

                    // Ausserhalb des Podiums: schlichter Text. Der Metallverlauf ist
                    // teuer und waere hier ohnehin unsichtbar.
                    Text {
                        id: rankPlain
                        text: row.position
                        visible: !row.podium
                        color: row.elimZone ? "#ff9090" : Theme.textMain
                        font { family: Theme.display; pixelSize: 27; weight: Font.Bold }
                        style: Text.Raised
                        styleColor: Qt.rgba(0, 0, 0, 0.7)
                    }

                    MedalText {
                        anchors.fill: parent
                        visible: row.podium
                        text: row.position
                        rank: row.position
                        font { family: Theme.display; pixelSize: 27; weight: Font.Bold }
                    }

                    // Positions-Flash: bei einer Ueberholung kurz aufpoppen.
                    SequentialAnimation {
                        id: posFlash
                        NumberAnimation { target: rankPlain.parent; property: "scale"
                                          from: 1.22; to: 1.0; duration: 1100
                                          easing.type: Easing.OutCubic }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    text: row.changeDir === "up" ? "▲" : row.changeDir === "down" ? "▼" : "-"
                    color: row.changeDir === "up" ? "#00ff88"
                         : row.changeDir === "down" ? "#ff3333" : "transparent"
                    font { family: Theme.sans; pixelSize: 13; weight: Font.Bold }
                    Behavior on color { ColorAnimation { duration: 300 } }
                }
            }
        }

        // 2 · Teamlogo + Farbstreifen
        Item {
            width: row.cols[1]
            height: parent.height

            Image {
                id: teamLogo
                anchors {
                    left: parent.left; right: strip.left; rightMargin: 4
                    verticalCenter: parent.verticalCenter
                }
                height: 30
                source: row.teamLogo ? Theme.teamLogo(row.teamLogo) : ""
                visible: row.teamLogo !== ""
                fillMode: Image.PreserveAspectFit
                smooth: true

                // ⚠ mipmap ist hier kein Feinschliff, sondern noetig.
                // Die Logos sind unterschiedlich geformt: Ferrari ist 149x202 und
                // wird kaum verkleinert, Aston Martin ist 256x58 und Cadillac
                // 248x94 - die passen nur ueber die BREITE in die 44-px-Zelle und
                // schrumpfen dabei auf ein Sechstel. Bei so viel Verkleinerung
                // greift die normale bilineare Filterung daneben (sie liest nur
                // 2x2 Texel) und das Logo wird koernig. Mit Mipmaps rechnet Qt
                // vorher passende Verkleinerungsstufen aus.
                mipmap: true

                // Nur so gross dekodieren, wie es am Ende gebraucht wird - sonst
                // haengt an jeder Zeile eine unnoetig grosse Textur. Beide Masse
                // setzen, weil breite Logos ueber die Breite begrenzt werden.
                sourceSize: Qt.size(Math.max(1, Math.round(width * 2)),
                                    Math.max(1, Math.round(height * 2)))
            }

            Rectangle {
                id: strip
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: 4
                height: 28
                radius: 2
                color: row.teamColor
                Behavior on color { ColorAnimation { duration: 400 } }
            }
        }

        // 3 · Fahrername + Team + Anbauten
        Item {
            width: row.cols[2]
            height: parent.height

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                spacing: 1

                Text {
                    width: parent.width
                    text: row.name
                    // Bestrunden-Fahrer bekommt einen lila Namen (.fl-lap .driver-name).
                    color: row.isFastestLap ? "#c44dff" : Theme.textMain
                    elide: Text.ElideRight
                    font { family: Theme.sans; pixelSize: 17; weight: Font.DemiBold
                           capitalization: Font.AllUppercase }
                    style: Text.Raised
                    styleColor: Qt.rgba(0, 0, 0, 0.7)
                }

                Row {
                    spacing: 6
                    height: 17

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.team
                        color: row.elimZone ? "#d9a0a0" : "#c2c2ce"
                        font { family: Theme.sans; pixelSize: 14; weight: Font.Bold
                               letterSpacing: 0.5 }
                    }

                    // Comeback: Plaetze seit dem Start (nur im Rennen).
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.comeback !== 0
                        text: (row.comeback > 0 ? "▲" : "▼") + Math.abs(row.comeback)
                        color: row.comeback > 0 ? "#00e676" : "#ff5a5a"
                        font { family: Theme.display; pixelSize: 13; weight: Font.Bold }
                    }

                    // Schaden an Front- und Heckfluegel.
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5

                        MaskedIcon {
                            width: 18; height: 17
                            visible: row.dmgFl >= 15
                            source: Theme.damage("FWL")
                            color: Fmt.dmgColor(row.dmgFl)
                            blinking: row.dmgFl > Kers.settings.dmgCrit
                            // .dw-fwl/.dw-fwr sind im CSS um beide Achsen gedreht.
                            rotation: 180
                        }
                        MaskedIcon {
                            width: 18; height: 17
                            visible: row.dmgFr >= 15
                            source: Theme.damage("FWR")
                            color: Fmt.dmgColor(row.dmgFr)
                            blinking: row.dmgFr > Kers.settings.dmgCrit
                            rotation: 180
                        }
                        MaskedIcon {
                            width: 26; height: 16
                            visible: row.dmgRw >= 15
                            source: Theme.damage("RW")
                            color: Fmt.dmgColor(row.dmgRw)
                            blinking: row.dmgRw > Kers.settings.dmgCrit
                        }
                    }
                }
            }
        }

        // 4 · LEADER (Rennen) bzw. BEST (Quali)
        Item {
            // Der Fuehrende bekommt ein Banner ueber beide Zeitspalten
            // (.col-gap.leader-banner { grid-column: span 2 }).
            width: row.isLeader ? row.cols[3] + 4 + row.cols[4] : row.cols[3]
            height: parent.height

            Text {
                anchors {
                    verticalCenter: parent.verticalCenter
                    right: row.isLeader ? undefined : parent.right
                    horizontalCenter: row.isLeader ? parent.horizontalCenter : undefined
                }
                text: row.gapText
                color: row.gapColor
                horizontalAlignment: row.isLeader ? Text.AlignHCenter : Text.AlignRight
                font {
                    family: Theme.sans
                    pixelSize: row.gapSmall ? (row.isLeader ? 15 : 14) : 18
                    weight: row.gapSmall ? Font.ExtraBold : Font.Bold
                    letterSpacing: row.isLeader ? 1 : (row.lapsDown >= 1 ? 0.5 : 0)
                    capitalization: row.isLeader ? Font.AllUppercase : Font.MixedCase
                    features: ({ "tnum": 1 })
                }
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.7)
            }
        }

        // 5 · INTERVAL (Rennen) bzw. GAP auf Pole (Quali)
        Item {
            visible: !row.isLeader
            width: row.cols[4]
            height: parent.height

            Text {
                anchors { verticalCenter: parent.verticalCenter; right: parent.right }
                text: row.lapText
                color: row.lapHasData ? Theme.textMain : Theme.textMuted
                font {
                    family: Theme.sans
                    pixelSize: row.lapHasData ? 18 : 12
                    weight: Font.Bold
                    features: ({ "tnum": 1 })
                }
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.7)
            }
        }

        // 6 · Reifen
        Item {
            width: row.cols[5]
            height: parent.height

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Item {
                    width: 30
                    height: 30
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: tyre
                        anchors.fill: parent
                        source: row.tyreIcon ? Theme.tyre(row.tyreIcon) : ""
                        visible: row.tyreIcon !== ""
                        fillMode: Image.PreserveAspectFit
                        sourceSize { width: 60; height: 60 }
                        smooth: true
                        transformOrigin: Item.Center
                    }

                    // Boxenstopp: der neue Reifen dreht sich herein (@keyframes tyre-spin-in).
                    ParallelAnimation {
                        id: tyreSpin
                        NumberAnimation { target: tyre; property: "rotation"
                                          from: -200; to: 0; duration: 600
                                          easing.type: Easing.Bezier
                                          easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1] }
                        NumberAnimation { target: tyre; property: "scale"
                                          from: 0.35; to: 1; duration: 600
                                          easing.type: Easing.Bezier
                                          easing.bezierCurve: [0.2, 0.8, 0.3, 1, 1, 1] }
                        NumberAnimation { target: tyre; property: "opacity"
                                          from: 0; to: 1; duration: 390 }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Rnd " + row.tyreAge
                    color: Theme.textMain
                    font { family: Theme.sans; pixelSize: 15; weight: Font.Bold
                           features: ({ "tnum": 1 }) }
                    style: Text.Raised
                    styleColor: Qt.rgba(0, 0, 0, 0.7)
                }
            }
        }

        // 7 · Sektoren. Haengt allein an der Spaltenbreite (Tower.qml, cols[6]) -
        // steht sie auf 0, ist die Spalte weg. Frueher hing sie an row.quali;
        // dann waere die Spalte bei Breite 0 zwar schmal, aber weiter sichtbar
        // gewesen, und die Zeiten haetten in die Nachbarspalte geragt (ein Item
        // beschneidet seine Kinder nicht).
        Item {
            visible: row.cols[6] > 0
            width: row.cols[6] || 0
            height: parent.height

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Repeater {
                    model: 3
                    Row {
                        spacing: 0
                        Text {
                            text: "S" + (index + 1) + ": "
                            color: row.lapInvalid ? "#ff6b6b" : "#dcdce4"
                            font { family: Theme.sans; pixelSize: 15
                                   features: ({ "tnum": 1 }) }
                            style: Text.Raised
                            styleColor: Qt.rgba(0, 0, 0, 0.7)
                        }
                        Text {
                            text: Fmt.time(row.sectors[index]) || "—"
                            color: Fmt.sectorColor(row.sectorClasses[index], row.lapInvalid)
                            font { family: Theme.sans; pixelSize: 15; weight: Font.Bold
                                   features: ({ "tnum": 1 }) }
                            style: Text.Raised
                            styleColor: Qt.rgba(0, 0, 0, 0.7)
                        }
                    }
                }
            }
        }

        // 8 · DRS / MOM (Rennen) bzw. Status (Quali)
        Item {
            width: row.quali ? row.cols[7] : row.cols[7]
            height: parent.height

            Rectangle {
                id: pill
                anchors { verticalCenter: parent.verticalCenter
                          horizontalCenter: parent.horizontalCenter }
                width: Math.min(parent.width, pillText.implicitWidth + (row.quali ? 12 : 8))
                height: pillText.implicitHeight + (row.quali ? 6 : 4)
                radius: 3
                visible: row.pillText !== ""
                color: row.pillBg
                border.width: 1
                border.color: row.pillBorder

                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: row.pillText
                    color: row.pillFg
                    font {
                        family: Theme.sans
                        pixelSize: row.quali ? 11 : 13
                        weight: Font.ExtraBold
                        letterSpacing: row.quali ? 0.7 : 0.2
                    }
                    style: Text.Raised
                    styleColor: Qt.rgba(0, 0, 0, 0.6)
                }

                // 2026er Overtake Mode: blauer Komet laeuft am Rand der Pille entlang.
                // Nur solange aktiv - deshalb im Loader, sonst haengt an jeder der 22
                // Zeilen dauerhaft ein Verlauf samt Maske im Szenengraphen.
                Loader {
                    anchors.fill: parent
                    active: row.overtakeActive
                    visible: active
                    sourceComponent: RunningBorder {
                        thickness: 2
                        frameRadius: 3
                        period: 6000
                        stops: [{ p: 0.000, c: "#96f0ff" },
                                { p: 0.056, c: Qt.rgba(90 / 255, 226 / 255, 1, 0.6) },
                                { p: 0.153, c: Qt.rgba(47 / 255, 217 / 255, 1, 0.28) },
                                { p: 0.278, c: Qt.rgba(47 / 255, 217 / 255, 1, 0.10) },
                                { p: 0.444, c: Qt.rgba(47 / 255, 217 / 255, 1, 0.0) }]
                    }
                }
            }
        }
    }

    // ── Abgeleitete Anzeige-Werte ───────────────────────────────────────────
    // Die if/else-Kette aus renderTower(): welche Zeit steht in welcher Spalte,
    // und in welcher Farbe. Als Properties statt inline, damit die Bindings oben
    // lesbar bleiben und jede Groesse nur einmal gerechnet wird.
    readonly property bool gapSmall: quali ? false
                                           : (isLeader || dnf || dsq || lapsDown >= 1)

    readonly property string gapText: {
        if (quali)
            return bestLap > 0 ? Fmt.time(bestLap) : "—";
        if (dsq) return "DSQ";
        if (dnf) return "DNF";
        if (isLeader) return "LEADER";
        if (lapsDown >= 1) return "+" + lapsDown + (lapsDown === 1 ? " LAP" : " LAPS");
        return Fmt.gap(gapToLeader);
    }

    readonly property color gapColor: {
        if (quali) return "#ffcc00";
        if (dsq) return "#ff6600";
        if (dnf) return "#9a9a9a";
        if (isLeader) return Theme.accent;
        return "#ffcc00";                       // auch fuer ueberrundet (.lapped)
    }

    readonly property bool lapHasData: quali ? (bestLap > 0 && poleTime > 0)
                                             : !(dnf || dsq)

    readonly property string lapText: {
        if (quali) {
            if (!(bestLap > 0 && poleTime > 0))
                return "—";
            var d = bestLap - poleTime;
            return d < 0.0005 ? "POLE" : Fmt.delta(d);
        }
        if (dnf || dsq) return "—";
        return Fmt.gap(gapToAhead);
    }

    // Die Pille rechts. Reihenfolge wie in renderTower(): Quali-Status schlaegt
    // alles, dann Ausgeschiedene, dann Box, dann DRS/Overtake.
    readonly property string pillText: {
        if (quali)
            return qualiStatus === "track" ? "HOTLAP" : qualiStatus === "inlap" ? "INLAP"
                 : qualiStatus === "out" ? "OUTLAP" : "BOX";
        if (dnf || dsq) return inPit ? "PIT" : "";
        if (inPit) return "PIT";
        return Kers.session.boostLabel;
    }

    readonly property color pillFg: {
        if (quali)
            return qualiStatus === "track" ? "#00e676" : qualiStatus === "out" ? "#ffd000"
                 : qualiStatus === "inlap" ? "#4db8ff" : "#9aa0aa";
        if (inPit) return "#ffc800";
        if (Kers.session.boostLabel === "MOM")
            return overtakeActive ? "#2fd9ff" : overtakeAvailable ? "#7fc7ff"
                                                                  : Theme.textMuted;
        return drs ? "#00ff88" : Theme.textMuted;
    }

    readonly property color pillBg: {
        if (quali)
            return qualiStatus === "track" ? Qt.rgba(0, 230 / 255, 118 / 255, 0.12)
                 : qualiStatus === "out" ? Qt.rgba(1, 208 / 255, 0, 0.12)
                 : qualiStatus === "inlap" ? Qt.rgba(77 / 255, 184 / 255, 1, 0.12)
                 : Qt.rgba(1, 1, 1, 0.06);
        if (inPit) return Qt.rgba(1, 200 / 255, 0, 0.12);
        if (Kers.session.boostLabel === "MOM")
            return overtakeActive ? Qt.rgba(0, 150 / 255, 1, 0.12)
                 : overtakeAvailable ? Qt.rgba(0, 150 / 255, 1, 0.06)
                 : Qt.rgba(1, 1, 1, 0.04);
        return drs ? Qt.rgba(0, 1, 136 / 255, 0.12) : Qt.rgba(1, 1, 1, 0.04);
    }

    readonly property color pillBorder: {
        if (quali)
            return qualiStatus === "track" ? Qt.rgba(0, 230 / 255, 118 / 255, 0.4)
                 : qualiStatus === "out" ? Qt.rgba(1, 208 / 255, 0, 0.4)
                 : qualiStatus === "inlap" ? Qt.rgba(77 / 255, 184 / 255, 1, 0.4)
                 : Qt.rgba(1, 1, 1, 0.14);
        if (inPit) return Qt.rgba(1, 200 / 255, 0, 0.35);
        if (Kers.session.boostLabel === "MOM")
            return overtakeActive ? Qt.rgba(0, 180 / 255, 1, 0.30)
                 : overtakeAvailable ? Qt.rgba(0, 150 / 255, 1, 0.22)
                 : Qt.rgba(1, 1, 1, 0.08);
        return drs ? Qt.rgba(0, 1, 136 / 255, 0.35) : Qt.rgba(1, 1, 1, 0.08);
    }

    // Die Stempel aus dem Modell zaehlen nur hoch - jede Aenderung ist ein Ereignis.
    onChangeStampChanged: if (changeStamp > 0) posFlash.restart()
    onTyreStampChanged: if (tyreStamp > 0) tyreSpin.restart()
}
