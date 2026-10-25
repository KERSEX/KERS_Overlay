// Timing Tower im Stil der F1-TV-Grafik (Design "F1 TV", ab 0.3.1).
//
// Vorlage: drei Bildschirmfotos der F1-TV-Uebertragung 2025/26 (Quali, Sprint,
// Safety Car). Ersetzt parts/Tower.qml des eingebauten Designs. Die Schnittstelle
// zu Overlay.qml bleibt dieselbe: stageHeight und groesse rein, width/height =
// die sichtbare Groesse (der Tower skaliert sich selbst, wie das Original).
//
// Aufbau von oben: Marke (eigenes Logo bzw. Titel aus /settings - ein F1-Logo
// liefert KERS nicht mit), Runden- bzw. Zeitleiste ("LAP 7 / 20", "Q2 0:49"),
// bei Safety Car / VSC / roter Flagge ein Banner, dann die Zeilen (F1TowerRow.qml).
//
// Ab 0.3.1 immer mit vollen Namen (breiter, extra.towerWidth) und Reifen mit
// Alter. Im Rennen wechselt der Wert alle extra.gapWechsel Sekunden (Vorgabe 8)
// zwischen Abstand zum Fuehrenden und Intervall zum Vordermann.

import QtQuick
import ".."
import "../Fmt.js" as Fmt

Item {
    id: tower

    property real stageHeight: 1080
    property real groesse: 1

    readonly property var ex: Theme.extra
    readonly property bool quali: Kers.session.isQuali
    readonly property int rowH: ex.rowHeight > 0 ? ex.rowHeight : 34
    /** Banner unter dem Kopf: "rf" | "sc" | "vsc" | "" - rote Flagge schlaegt SC. */
    readonly property string banner: Kers.session.redFlag ? "rf"
                                   : Kers.session.scStatus === "sc" ? "sc"
                                   : Kers.session.scStatus === "vsc" ? "vsc" : ""
    /** Rennen: Intervall zum Vordermann statt Abstand zum Fuehrenden - wechselt per Uhr. */
    property bool intervall: false
    Timer {
        running: !tower.quali && Kers.session.connected
        repeat: true
        interval: (tower.ex.gapWechsel > 0 ? tower.ex.gapWechsel : 8) * 1000
        onTriggered: tower.intervall = !tower.intervall
        onRunningChanged: if (!running) tower.intervall = false
    }

    readonly property int rowCount: Kers.drivers.count
    property int maxRowsSeen: 0
    onRowCountChanged: maxRowsSeen = Math.max(maxRowsSeen, rowCount)
    // Immer gleich hoch rechnen (volles Feld), sonst wuerden die Zeilen bei jedem
    // Ausfall groesser - wie im eingebauten Tower.
    readonly property int towerRows: Kers.settings.rows > 0 ? Kers.settings.rows
                                                           : Math.max(22, maxRowsSeen)

    readonly property real panelWidth: ex.towerWidth > 0 ? ex.towerWidth : 340

    visible: Kers.settings.showTower
    width: panelWidth * panel.scale
    height: panel.height * panel.scale

    readonly property real autoFit: {
        var refH = kopf.height + 34 + towerRows * rowH + fuss.height;
        var availH = (stageHeight - 40) * 0.80;
        return refH > 0 ? Math.min(1.6, availH / refH) : 1;
    }

    Item {
        id: panel
        width: tower.panelWidth
        height: kopf.height + band.height + body.height + fuss.height
        transformOrigin: Item.TopLeft
        scale: tower.autoFit * (Kers.settings.scale > 0 ? Kers.settings.scale : 1) * tower.groesse

        Rectangle {
            anchors.fill: parent
            radius: Theme.panelRadius
            color: Theme.panelBg
            border.width: 1
            border.color: Theme.panelBorder
        }

        // ── Kopf: Marke + Runden-/Zeitleiste ─────────────────────────────────
        Column {
            id: kopf
            width: parent.width

            Item {
                width: parent.width
                height: 58

                Image {
                    id: logo
                    anchors.centerIn: parent
                    visible: Kers.settings.towerLogo !== ""
                    source: Theme.brandLogo(Kers.settings.towerLogo)
                    height: Math.min(46, Kers.settings.towerLogoH > 0 ? Kers.settings.towerLogoH : 40)
                    width: Math.min(parent.width - 24, implicitWidth * height / Math.max(1, implicitHeight))
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    sourceSize.height: Math.max(1, Math.round(height * 2))
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    visible: !logo.visible
                    text: Kers.settings.brandTitle !== "" ? Kers.settings.brandTitle
                                                          : (tower.quali ? "QUALIFYING" : "GRAND PRIX")
                    elide: Text.ElideRight
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 21; weight: Font.Black; italic: true
                           letterSpacing: 1.5; capitalization: Font.AllUppercase }
                }
            }

            Rectangle {
                width: parent.width
                height: 32
                color: tower.ex.lapBar || Theme.headerRowBg

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.baseline: wert.baseline
                        text: tower.quali ? (Kers.session.sessionName !== "" ? Kers.session.sessionName : "Q") : "LAP"
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 15; weight: Font.Medium; letterSpacing: 1 }
                    }
                    Text {
                        id: wert
                        text: tower.quali ? (Kers.session.timeLeft > 0 ? Fmt.clock(Kers.session.timeLeft) : "")
                                          : (Kers.session.currentLap > 0 ? Kers.session.currentLap : "–")
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 18; weight: Font.Black; features: ({ "tnum": 1 }) }
                    }
                    Text {
                        anchors.baseline: wert.baseline
                        visible: !tower.quali && Kers.session.totalLaps > 0
                        text: "/ " + Kers.session.totalLaps
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 15; weight: Font.Medium; features: ({ "tnum": 1 }) }
                    }
                }
            }
        }

        // ── Banner: Safety Car / VSC / rote Flagge ───────────────────────────
        Rectangle {
            id: band
            anchors.top: kopf.bottom
            width: parent.width
            height: tower.banner !== "" ? 34 : 0
            visible: height > 0
            clip: true
            color: tower.banner === "rf" ? (tower.ex.redFlag || "#e10600") : (tower.ex.scYellow || "#ffd200")
            Behavior on height { NumberAnimation { duration: 250 } }

            Text {
                anchors.centerIn: parent
                text: tower.banner === "rf" ? "RED FLAG"
                    : tower.banner === "vsc" ? "VIRTUAL SAFETY CAR" : "SAFETY CAR"
                color: tower.banner === "rf" ? "#ffffff" : "#111111"
                font { family: Theme.display; pixelSize: tower.banner === "vsc" ? 15 : 18; weight: Font.Black
                       letterSpacing: 1 }
            }
            // VSC pulsiert leicht - wie die Grafik im Fernsehen
            SequentialAnimation on opacity {
                running: tower.banner === "vsc"
                loops: Animation.Infinite
                NumberAnimation { to: 0.65; duration: 800; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                onRunningChanged: if (!running) band.opacity = 1
            }
        }

        // ── Zeilen ───────────────────────────────────────────────────────────
        Item {
            id: body
            anchors.top: band.bottom
            width: parent.width
            height: tower.rowCount * tower.rowH

            Repeater {
                model: Kers.drivers
                delegate: F1TowerRow {
                    width: body.width
                    rowH: tower.rowH
                    quali: tower.quali
                    intervall: tower.intervall
                    poleTime: Kers.session.poleTime
                }
            }

            // Elimination-Grenze in der Quali
            Rectangle {
                visible: Kers.session.elimCut > 0 && Kers.session.elimCut < tower.rowCount
                y: Kers.session.elimCut * tower.rowH - 1
                width: parent.width
                height: 2
                color: tower.ex.redFlag || "#e10600"
                z: 5
            }
        }

        Item {
            id: fuss
            anchors.top: body.bottom
            width: parent.width
            height: 10
        }

        // ── Noch keine Daten ─────────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent
            visible: !Kers.session.connected && !Kers.session.holdResult
            radius: Theme.panelRadius
            color: Qt.rgba(0.07, 0.07, 0.09, 0.94)
            z: 20
            Column {
                anchors.centerIn: parent
                spacing: 8
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "WARTE AUF"
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 18; weight: Font.Black; letterSpacing: 2 }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "TELEMETRIE"
                    color: Theme.textMain
                    font { family: Theme.display; pixelSize: 18; weight: Font.Black; letterSpacing: 2 }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Kers.session.linked ? "UDP 2026 · Port 20777" : "Kein Kontakt zum Server"
                    color: Theme.textMuted
                    font { family: Theme.sans; pixelSize: 12 }
                }
            }
        }
    }
}
