// Eine Zeile des F1-TV-Towers (Design "F1 TV", ab 0.3.1).
//
//   [Pos] [Logo] VOLLER NAME [+5] (⏱) ....... +0.7   (12) ▌
//
// Position in eigener dunkler Spalte (P1 rot), kurz nach einem Ueberholmanoever
// ein gruenes ▲ bzw. rotes ▼ statt der Zahl. Dahinter immer der volle Name (KERS'
// Wunsch statt der drei Buchstaben), Strafe, lila Stoppuhr bei der schnellsten
// Runde.
//
// Wert im Rennen: wechselt mit dem Tower (`intervall`) zwischen Abstand zum
// Fuehrenden und Abstand zum Vordermann - die P1-Zeile sagt dann "Leader" bzw.
// "Interval", wie im Fernsehen. "IN PIT" in Teamfarbe, "Out" bei Ausfall.
// Quali: Bestzeit bzw. Rueckstand auf die Pole. Rechts immer der Reifen als
// "(12)" - Halbkreise in der Farbe der Mischung, dazwischen das Alter in Runden
// (F1Reifen.qml) -, ganz aussen der gruene Strich bei DRS/Overtake.
//
// Die Zeile sitzt selbst auf ihrem Platz (y = slot * rowH) und gleitet beim
// Positionswechsel dorthin - wie im eingebauten Tower. Der Kamera-Fahrer bekommt
// eine hellere Zeile, einen weissen Strich und einen kleinen Pfeil rechts neben
// dem Tower.

import QtQuick
import ".."
import "../Fmt.js" as Fmt
import "F1.js" as F1

Item {
    id: row

    property int rowH: 34
    property bool quali: false
    property bool intervall: false      // Rennen: Abstand zum Vordermann statt zum Fuehrenden
    property real poleTime: 0
    readonly property var ex: Theme.extra

    required property int slot
    required property int position
    required property string name
    required property color teamColor
    required property string teamLogo
    required property bool isFocused
    required property bool isFastestLap
    required property bool elimZone
    required property string changeDir
    required property int changeStamp
    required property real gapToLeader
    required property real gapToAhead
    required property real bestLap
    required property int lapsDown
    required property bool dnf
    required property bool dsq
    required property bool inPit
    required property string compound
    required property int tyreAge
    required property string qualiStatus
    required property bool drs
    required property bool overtakeActive
    required property int penalties

    height: rowH
    y: slot * rowH
    property bool settled: false
    Component.onCompleted: settled = true
    Behavior on y {
        enabled: row.settled
        NumberAnimation { duration: 600; easing.type: Easing.Bezier; easing.bezierCurve: [0.65, 0, 0.35, 1, 1, 1] }
    }

    readonly property bool raus: dnf || dsq
    readonly property color farbeRaus: ex.out || "#7c7e88"

    // Spalten rechts (Breite des Werts, des Reifens) - der Name nimmt den Rest.
    readonly property int wertW: 64
    readonly property int reifenW: 32

    // ── Positionswechsel: drei Sekunden lang Pfeil statt Zahl ────────────────
    property bool pfeil: false
    Timer { id: pfeilUhr; interval: 3000; onTriggered: row.pfeil = false }
    onChangeStampChanged: {
        if (changeStamp > 0 && (changeDir === "up" || changeDir === "down")) {
            pfeil = true;
            pfeilUhr.restart();
        }
    }

    /** Status, der in beiden Rennansichten gleich bleibt - sonst "". */
    readonly property string status: dsq ? "DSQ" : dnf ? "Out" : inPit ? "IN PIT" : ""

    readonly property string leaderText: {
        if (quali) {
            if (bestLap > 0) {
                if (poleTime > 0 && bestLap - poleTime < 0.0005)
                    return Fmt.time(bestLap);
                return poleTime > 0 ? "+" + (bestLap - poleTime).toFixed(3) : Fmt.time(bestLap);
            }
            return qualiStatus === "out" ? "Out Lap" : qualiStatus === "inlap" ? "In Lap"
                 : qualiStatus === "track" ? "Hot Lap" : "No Time";
        }
        if (status !== "") return status;
        if (position === 1) return "Leader";
        if (lapsDown >= 1) return "+" + lapsDown + (lapsDown === 1 ? " Lap" : " Laps");
        return gapToLeader > 0 ? "+" + gapToLeader.toFixed(1) : "+0.0";
    }
    readonly property string intervallText: {
        if (status !== "") return status;
        if (position === 1) return "Interval";
        return gapToAhead > 0 ? "+" + gapToAhead.toFixed(1) : "+0.0";
    }
    readonly property color wertFarbe: {
        if (raus) return farbeRaus;
        if (!quali && inPit) return teamColor;
        if (quali && !(bestLap > 0)) return Theme.textMuted;
        return Theme.textMain;
    }
    readonly property bool wertFett: (!quali && inPit) || (quali && poleTime > 0 && bestLap > 0 && bestLap - poleTime < 0.0005)

    // ── Kamera-Fahrer ("auf wen bin ich drauf") ──────────────────────────────
    // Farben ueber design.json: extra.focus (Strich + Pfeil), extra.focusBg
    // (Zeile, #AARRGGBB).
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 34
        color: row.ex.focusBg || Qt.rgba(1, 1, 1, 0.16)
        visible: row.isFocused
    }
    Rectangle {
        x: 34
        width: 3
        height: parent.height
        color: row.ex.focus || "#ffffff"
        visible: row.isFocused
        z: 2
    }
    // Nur die markierte Zeile legt den Pfeil an - nicht 22 Canvas-Flaechen.
    Loader {
        x: row.width + 3
        anchors.verticalCenter: parent.verticalCenter
        active: row.isFocused
        sourceComponent: Canvas {
            width: 8
            height: 14
            readonly property color farbe: row.ex.focus || "#ffffff"
            onFarbeChanged: requestPaint()
            onPaint: {
                var c = getContext("2d");
                c.reset();
                c.fillStyle = farbe;
                c.beginPath();
                c.moveTo(width, 0);
                c.lineTo(0, height / 2);
                c.lineTo(width, height);
                c.closePath();
                c.fill();
            }
        }
    }

    // ── Position ─────────────────────────────────────────────────────────────
    Rectangle {
        id: posBox
        width: 34
        height: parent.height
        color: row.position === 1 && !row.raus ? (row.ex.p1Bg || Theme.accent)
             : row.elimZone ? Qt.rgba(0.88, 0.02, 0, 0.35) : (row.ex.posBg || "#14151a")
        Text {
            anchors.centerIn: parent
            visible: !row.pfeil
            text: row.position
            color: row.raus ? row.farbeRaus : Theme.textMain
            font { family: Theme.display; pixelSize: 15; weight: Font.DemiBold; features: ({ "tnum": 1 }) }
        }
        Text {
            anchors.centerIn: parent
            visible: row.pfeil
            text: row.changeDir === "up" ? "▲" : "▼"
            color: row.changeDir === "up" ? (row.ex.gain || "#2ad04b") : (row.ex.loss || "#e10600")
            font.pixelSize: 13
        }
    }

    Image {
        x: 41
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        source: row.teamLogo ? Theme.teamLogo(row.teamLogo) : ""
        visible: row.teamLogo !== ""
        fillMode: Image.PreserveAspectFit
        sourceSize { width: 40; height: 40 }
        smooth: true
        mipmap: true
        opacity: row.raus ? 0.45 : 1
    }

    // ── Name, Strafe, schnellste Runde ───────────────────────────────────────
    Row {
        id: nameZeile
        x: 68
        height: parent.height
        spacing: 5
        // Was rechts vom Namen noch Platz braucht, geht vom Namen ab.
        readonly property real platz: row.width - 68 - 8 - row.wertW - 8 - row.reifenW - 10

        Text {
            id: nameText
            width: Math.min(implicitWidth, nameZeile.platz
                            - (strafe.visible ? strafe.width + nameZeile.spacing : 0)
                            - (uhr.active ? 15 + nameZeile.spacing : 0))
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: row.name
            elide: Text.ElideRight
            color: row.raus ? row.farbeRaus : Theme.textMain
            font { family: Theme.display; pixelSize: 15; weight: Font.Bold; letterSpacing: 0.3
                   capitalization: Font.AllUppercase }
        }
        // Strafe (Rennen): kleines rotes Kaestchen hinter dem Namen
        Rectangle {
            id: strafe
            visible: !row.quali && row.penalties > 0 && !row.raus
            anchors.verticalCenter: parent.verticalCenter
            width: strafeText.implicitWidth + 6
            height: 15
            radius: 2
            color: row.ex.loss || "#e10600"
            Text {
                id: strafeText
                anchors.centerIn: parent
                text: "+" + row.penalties
                color: "#ffffff"
                font { family: Theme.display; pixelSize: 11; weight: Font.Bold }
            }
        }
        // Schnellste Runde: lila Stoppuhr - nur in der Zeile, die sie haelt
        Loader {
            id: uhr
            anchors.verticalCenter: parent.verticalCenter
            active: !row.quali && row.isFastestLap && !row.raus
            visible: active
            sourceComponent: Canvas {
                width: 15
                height: 15
                readonly property color farbe: row.ex.purple || "#8f2ad8"
                onFarbeChanged: requestPaint()
                onPaint: {
                    var c = getContext("2d");
                    c.reset();
                    var r = width / 2;
                    c.fillStyle = farbe;
                    c.beginPath();
                    c.arc(r, r, r, 0, Math.PI * 2);
                    c.fill();
                    c.strokeStyle = "#ffffff";
                    c.lineWidth = 1.4;
                    c.beginPath();                       // Zifferblatt
                    c.arc(r, r + 0.8, r - 3.6, 0, Math.PI * 2);
                    c.stroke();
                    c.beginPath();                       // Zeiger
                    c.moveTo(r, r + 0.8);
                    c.lineTo(r + 2.2, r - 1.6);
                    c.stroke();
                    c.fillStyle = "#ffffff";             // Krone
                    c.fillRect(r - 1.2, 1.2, 2.4, 1.6);
                }
            }
        }
    }

    // ── Wert: Abstand / Zeit / Status ────────────────────────────────────────
    // Zwei Texte, die ueberblenden: Leader-Abstand und Intervall.
    Item {
        anchors { right: reifen.left; rightMargin: 8; top: parent.top; bottom: parent.bottom }
        width: row.wertW
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: row.leaderText
            opacity: row.quali || !row.intervall ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
            color: row.wertFarbe
            font { family: Theme.display; pixelSize: 15
                   weight: row.wertFett ? Font.Bold : Font.Medium; features: ({ "tnum": 1 }) }
        }
        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: !row.quali
            text: row.intervallText
            opacity: !row.quali && row.intervall ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }
            color: row.wertFarbe
            font { family: Theme.display; pixelSize: 15
                   weight: row.wertFett ? Font.Bold : Font.Medium; features: ({ "tnum": 1 }) }
        }
    }

    // ── Reifen: "(12)" - Halbkreise in der Farbe der Mischung, dazwischen das Alter
    F1Reifen {
        id: reifen
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
        compound: row.compound
        alter: row.tyreAge
        opacity: row.raus ? 0.45 : 1
    }

    // DRS/Overtake: gruener Strich ganz aussen
    Rectangle {
        anchors { right: parent.right; rightMargin: 2; verticalCenter: parent.verticalCenter }
        visible: !row.quali && !row.raus && !row.inPit && (row.drs || row.overtakeActive)
        width: 3
        height: 16
        radius: 1
        color: row.ex.gain || "#2ad04b"
    }
}
