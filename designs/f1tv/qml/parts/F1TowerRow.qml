// Eine Zeile des F1-TV-Towers (Design "F1 TV", ab 0.3.1).
//
//   [Pos] [Logo] KÜRZEL ............ +0.7 ▌
//
// Position in eigener dunkler Spalte (P1 rot), kurz nach einem Ueberholmanoever
// ein gruenes ▲ bzw. rotes ▼ statt der Zahl. Rennen: Abstand zum Fuehrenden mit
// einer Nachkommastelle, "Leader", "IN PIT" in Teamfarbe, "Out" bei Ausfall,
// rechts ein gruener Strich bei DRS/Overtake. Quali: Bestzeit bzw. Rueckstand auf
// die Pole und rechts der Reifen. Unter Safety Car stehen volle Nachnamen.
//
// Die Zeile sitzt selbst auf ihrem Platz (y = slot * rowH) und gleitet beim
// Positionswechsel dorthin - wie im eingebauten Tower.
//
// Der Kamera-Fahrer bekommt eine hellere Zeile, einen weissen Strich und einen
// kleinen Pfeil rechts neben dem Tower (ab 0.3.1).

import QtQuick
import ".."
import "../Fmt.js" as Fmt
import "F1.js" as F1

Item {
    id: row

    property int rowH: 34
    property bool quali: false
    property bool namenModus: false
    property real poleTime: 0
    readonly property var ex: Theme.extra

    required property int slot
    required property int position
    required property string name
    required property color teamColor
    required property string teamLogo
    required property bool isFocused
    required property bool elimZone
    required property string changeDir
    required property int changeStamp
    required property real gapToLeader
    required property real bestLap
    required property int lapsDown
    required property bool dnf
    required property bool dsq
    required property bool inPit
    required property string compound
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

    // ── Positionswechsel: drei Sekunden lang Pfeil statt Zahl ────────────────
    property bool pfeil: false
    Timer { id: pfeilUhr; interval: 3000; onTriggered: row.pfeil = false }
    onChangeStampChanged: {
        if (changeStamp > 0 && (changeDir === "up" || changeDir === "down")) {
            pfeil = true;
            pfeilUhr.restart();
        }
    }

    readonly property string wertText: {
        if (quali) {
            if (bestLap > 0) {
                if (poleTime > 0 && bestLap - poleTime < 0.0005)
                    return Fmt.time(bestLap);
                return poleTime > 0 ? "+" + (bestLap - poleTime).toFixed(3) : Fmt.time(bestLap);
            }
            return qualiStatus === "out" ? "Out Lap" : qualiStatus === "inlap" ? "In Lap"
                 : qualiStatus === "track" ? "Hot Lap" : "No Time";
        }
        if (dsq) return "DSQ";
        if (dnf) return "Out";
        if (inPit) return "IN PIT";
        if (position === 1) return "Leader";
        if (lapsDown >= 1) return "+" + lapsDown + (lapsDown === 1 ? " Lap" : " Laps");
        return gapToLeader > 0 ? "+" + gapToLeader.toFixed(1) : "+0.0";
    }
    readonly property color wertFarbe: {
        if (raus) return ex.out || "#7c7e88";
        if (!quali && inPit) return teamColor;
        if (quali && !(bestLap > 0)) return Theme.textMuted;
        return Theme.textMain;
    }
    readonly property bool wertFett: (!quali && inPit) || (quali && poleTime > 0 && bestLap > 0 && bestLap - poleTime < 0.0005)

    // ── Kamera-Fahrer ("auf wen bin ich drauf") ──────────────────────────────
    // Hellere Zeile, weisser Strich an der Positionsspalte und ein kleiner
    // Pfeil, der von aussen auf die Zeile zeigt. Farben ueber design.json:
    // extra.focus (Strich + Pfeil), extra.focusBg (Zeile, #AARRGGBB).
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
            color: row.raus ? (row.ex.out || "#7c7e88") : Theme.textMain
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
        id: logo
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

    Text {
        id: nameText
        x: 68
        width: row.namenModus ? row.width - 68 - 12 : 56
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        text: row.namenModus ? F1.nachname(row.name) : F1.kuerzel(row.name)
        elide: Text.ElideRight
        color: row.raus ? (row.ex.out || "#7c7e88") : Theme.textMain
        font { family: Theme.display; pixelSize: 16; weight: Font.Bold; letterSpacing: 0.4
               capitalization: Font.AllUppercase }
    }

    // Strafe (Rennen): kleines rotes Kaestchen hinter dem Kuerzel
    Rectangle {
        visible: !row.quali && !row.namenModus && row.penalties > 0 && !row.raus
        x: nameText.x + Math.min(nameText.contentWidth, nameText.width) + 4
        anchors.verticalCenter: parent.verticalCenter
        width: strafe.implicitWidth + 6
        height: 15
        radius: 2
        color: row.ex.loss || "#e10600"
        Text {
            id: strafe
            anchors.centerIn: parent
            text: "+" + row.penalties
            color: "#ffffff"
            font { family: Theme.display; pixelSize: 11; weight: Font.Bold }
        }
    }

    // ── Wert: Abstand / Zeit / Status ────────────────────────────────────────
    Text {
        visible: !row.namenModus
        anchors { right: mark.left; rightMargin: 7; verticalCenter: parent.verticalCenter }
        text: row.wertText
        color: row.wertFarbe
        font { family: Theme.display; pixelSize: 15
               weight: row.wertFett ? Font.Bold : Font.Medium; features: ({ "tnum": 1 }) }
    }

    // ── Rechts: Reifen (Quali) bzw. DRS-/Overtake-Strich (Rennen) ────────────
    Item {
        id: mark
        anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
        width: row.quali ? 12 : 4
        height: 18
        visible: !row.namenModus

        Text {
            anchors.centerIn: parent
            visible: row.quali && row.compound !== "" && row.compound !== "?"
            text: row.compound
            color: F1.reifenFarbe(row.compound, Theme.textMuted)
            font { family: Theme.display; pixelSize: 13; weight: Font.Black }
        }
        Rectangle {
            anchors.centerIn: parent
            visible: !row.quali && !row.raus && !row.inPit && (row.drs || row.overtakeActive)
            width: 3
            height: 16
            radius: 1
            color: row.ex.gain || "#2ad04b"
        }
    }
}
