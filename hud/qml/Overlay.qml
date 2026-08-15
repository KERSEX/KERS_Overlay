// Die Wurzel der Overlay-Szene.
//
// Hier haengen alle Bausteine drin, jeder an seinem festen Platz - das Gegenstueck
// zu templates/index.html, wo jeder Baustein per `position: fixed` sitzt. Die
// Randabstaende unten sind dieselben Zahlen wie im CSS.
//
// Im Web gibt es zusaetzlich die Einzelseiten /part/<name> fuer eigene
// OBS-Browserquellen; hier ist alles EINE Szene, und was sichtbar ist, entscheiden
// die Settings (Kers.settings.showTower und Geschwister) sowie die Regie.

import QtQuick
import "parts"

Item {
    id: root

    // Kein eigener Hintergrund: die Durchsichtigkeit macht das Fenster selbst
    // (setColor in qml_overlay.py). Im Chroma-Key-Modus setzt Python dort die
    // Schluesselfarbe ein - auch dann braucht es hier nichts.

    // ── Freies Layout ───────────────────────────────────────────────────────
    // Jeder Baustein haengt an einem der neun Ankerpunkte (tl tc tr / lc cc rc /
    // bl bc br) und bekommt von dort einen Versatz. Ankerpunkt statt absoluter
    // Koordinaten, damit ein Baustein unten rechts auch unten rechts bleibt,
    // wenn das Fenster eine andere Groesse hat (HUD 2560x1440, OBS 1920x1080).
    //
    // ⚠ Der Rueckfall ist der Kern: OHNE Eintrag gilt weiter der Ausdruck, der
    // vorher fest hier stand. Vier Bausteine rechnen naemlich dynamisch - der
    // Tower je nach Strafen-Seite, die Ampel bei 12 % der Hoehe, die
    // Pit-Projektion je nach sichtbarem Onboard, die Trackmap ueber mapcorner.
    // Ein fester Zahlenwert waere dort ein Rueckschritt. Erst wenn man einen
    // Baustein wirklich anfasst, entsteht sein Eintrag und gewinnt.
    function teil(name) {
        const L = Kers.settings.layout;
        return (L && L[name]) ? L[name] : null;
    }
    function lx(name, w, standard) {
        const t = teil(name);
        if (!t) return standard;
        const e = t.ecke || "tl";
        if (e === "tl" || e === "lc" || e === "bl") return t.dx;
        if (e === "tr" || e === "rc" || e === "br") return root.width - w - t.dx;
        return Math.round((root.width - w) / 2) + t.dx;      // tc / cc / bc
    }
    function ly(name, h, standard) {
        const t = teil(name);
        if (!t) return standard;
        const e = t.ecke || "tl";
        if (e === "tl" || e === "tc" || e === "tr") return t.dy;
        if (e === "bl" || e === "bc" || e === "br") return root.height - h - t.dy;
        return Math.round((root.height - h) / 2) + t.dy;      // lc / cc / rc
    }
    function lz(name, standard) {
        const t = teil(name);
        return (t && t.z !== undefined) ? t.z : standard;
    }

    // ── Umkehrung: aus einer Bildschirmposition den Versatz zurueckrechnen ───
    // Braucht das Ziehen. Muss zu lx/ly passen, sonst springt ein Baustein beim
    // Loslassen.
    function dxAus(ecke, x, w) {
        if (ecke === "tl" || ecke === "lc" || ecke === "bl") return Math.round(x);
        if (ecke === "tr" || ecke === "rc" || ecke === "br") return Math.round(root.width - w - x);
        return Math.round(x - Math.round((root.width - w) / 2));
    }
    function dyAus(ecke, y, h) {
        if (ecke === "tl" || ecke === "tc" || ecke === "tr") return Math.round(y);
        if (ecke === "bl" || ecke === "bc" || ecke === "br") return Math.round(root.height - h - y);
        return Math.round(y - Math.round((root.height - h) / 2));
    }

    /** Naechstgelegener Ankerpunkt zur Mitte des Bausteins - fuer noch nie
     *  verschobene Bausteine, die noch keinen eigenen haben. Wer schon einen
     *  hat, behaelt ihn: sonst spraenge die Bezugsecke beim kleinsten Ziehen um. */
    function eckeNah(x, y, w, h) {
        const mx = x + w / 2, my = y + h / 2;
        const sp = mx < root.width / 3 ? "l" : (mx < root.width * 2 / 3 ? "c" : "r");
        const ze = my < root.height / 3 ? "t" : (my < root.height * 2 / 3 ? "m" : "b");
        if (ze === "t") return sp === "l" ? "tl" : (sp === "c" ? "tc" : "tr");
        if (ze === "b") return sp === "l" ? "bl" : (sp === "c" ? "bc" : "br");
        return sp === "l" ? "lc" : (sp === "c" ? "cc" : "rc");
    }

    // Ungefaehre Groesse je Baustein - NUR fuer die Platzhalter beim Bearbeiten.
    // Die Vorschau speist zwar erfundene Renndaten ein, aber die Quali-Bausteine
    // (Hotlap-Boxen, Gefahrenzone) erscheinen darin nie, und ein unsichtbarer
    // Baustein ist 0 px gross - ohne Platzhalter kaeme man an ihn nicht heran.
    // Bewusst hier und nicht im Server: gebraucht wird es allein an dieser Stelle.
    readonly property var teilGroessen: ({
        "tower":      [700, 620], "trackmap": [480, 480], "racemsg":  [380, 50],
        "flbanner":   [520, 90],  "lights":   [420, 90],  "battles":  [640, 220],
        "hotlaps":    [630, 210], "lowerthird": [560, 120], "danger": [460, 90],
        "onboard":    [230, 300], "pitproj":  [230, 120], "pitcards": [300, 200],
        "champ":      [420, 300]
    })

    /** Der echte Baustein zu einem Schluessel. */
    function echtes(name) {
        for (let i = 0; i < root.children.length; i++)
            if (root.children[i].objectName === name) return root.children[i];
        return null;
    }
    /** Platzhalter heissen "ph:tower" - hier faellt das Praefix weg. */
    function schluessel(item) {
        const n = item ? item.objectName : "";
        return n.indexOf("ph:") === 0 ? n.substring(3) : n;
    }

    /** Welcher Baustein liegt unter diesem Punkt? Der oberste gewinnt.
     *  Nur Elemente MIT objectName kommen in Frage - die Bearbeiten-Flaeche und
     *  der Fensterrahmen haben keinen und fallen damit von selbst heraus. */
    function teilUnter(px, py) {
        let treffer = null;
        for (let i = 0; i < root.children.length; i++) {
            const c = root.children[i];
            if (!c.visible || !c.objectName) continue;
            if (px < c.x || px > c.x + c.width || py < c.y || py > c.y + c.height) continue;
            if (!treffer || c.z >= treffer.z) treffer = c;
        }
        return treffer;
    }

    // ── Timing Tower: oben links ────────────────────────────────────────────
    Tower {
        objectName: "tower"
        // Der linke Abstand existiert NUR, um die Strafen-Pillen aufzufangen, die
        // links aus der Zeile herausragen (im CSS: padding-left am body, dort 72,
        // hier auf Wunsch um ein Drittel verringert auf 48).
        //
        // Stehen die Pillen rechts (Setting penside), braucht es links gar keine
        // Reserve mehr - dann reicht derselbe Randabstand wie bei der Trackmap.
        // Der Tower rueckt damit von selbst nach links, sobald du umstellst, und
        // wieder zurueck, wenn du es rueckgaengig machst.
        x: root.lx("tower", width, Kers.settings.penSide === "right" ? 12 : 48)
        y: root.ly("tower", height, 10)
        stageHeight: root.height
        z: root.lz("tower", 30)
    }

    // ── Trackmap: Platz aus den Settings (mapcorner) ────────────────────────
    // Bis 08/2026 hing sie fest oben rechts - die Auswahl in /settings war im
    // QML-Renderer damit wirkungslos (im Web arbeitet applyMapCorner()).
    // Nicht angeboten werden "oben links" und "links mitte": dort steht der Tower.
    //
    // ⚠ Bewusst x/y statt umschaltbarer anchors: ein Anker laesst sich mit
    // `undefined` NICHT verlaesslich wieder abschalten. Beim ersten Versuch hing
    // die Karte dadurch gleichzeitig an linker und rechter Kante und wurde auf
    // Fensterbreite gezogen - statt 480 px war sie ueber 2000 px breit. Mit x/y
    // bleibt `width: src.size` aus Trackmap.qml unangetastet.
    Trackmap {
        id: trackmap
        objectName: "trackmap"
        z: root.lz("trackmap", 42)

        // Unbekannte Werte (z.B. das abgeschaffte "tl" aus einer aelteren
        // overlay_settings.json) auf "tr" biegen.
        readonly property string corner: {
            const c = Kers.settings.mapCorner || "tr";
            return ["tc", "tr", "rc", "bl", "bc", "br"].indexOf(c) >= 0 ? c : "tr";
        }
        // Abstand zur Bildschirmkante. Dazu kommt der Rand INNERHALB der Karte
        // (~29 px bei 480 px, s. FIT_TARGET in extras.py) - zusammen ergibt das den
        // sichtbaren Abstand. Kleiner = naeher an die Kante.
        readonly property int gap: 12

        // Ohne Layout-Eintrag gilt weiter mapcorner - sonst haetten wir zwei
        // Stellen, die dieselbe Karte verschieben wollen.
        x: root.lx("trackmap", width,
                   corner === "bl" ? gap
                   : (corner === "tc" || corner === "bc") ? Math.round((parent.width - width) / 2)
                   : parent.width - width - gap)
        y: root.ly("trackmap", height,
                   (corner === "tc" || corner === "tr") ? gap
                   : corner === "rc" ? Math.round((parent.height - height) / 2)
                   : parent.height - height - gap)
    }

    // ── Meldungen und Banner: oben mittig ───────────────────────────────────
    // ⚠ Die frueheren `anchors.horizontalCenter` sind hier ueberall gegen ein
    // gerechnetes x getauscht. Ein Anker gewinnt immer gegen x - beides
    // gleichzeitig geht nicht, und abschalten laesst er sich nicht verlaesslich
    // (siehe die Warnung bei der Trackmap). Der Rueckfallwert bildet exakt das
    // ab, was der Anker vorher tat.
    RaceMessage {
        objectName: "racemsg"
        x: root.lx("racemsg", width, Math.round((root.width - width) / 2))
        baseY: root.ly("racemsg", height, 22)
        z: root.lz("racemsg", 46)
    }

    FastestLapBanner {
        objectName: "flbanner"
        x: root.lx("flbanner", width, Math.round((root.width - width) / 2))
        baseY: root.ly("flbanner", height, 84)
        z: root.lz("flbanner", 47)
    }

    StartLights {
        objectName: "lights"
        x: root.lx("lights", width, Math.round((root.width - width) / 2))
        y: root.ly("lights", height, root.height * 0.12)
        z: root.lz("lights", 80)
    }

    // ── Unten mittig: Battle-Boxen im Rennen, Hotlap-Boxen in der Quali ─────
    // Beide sitzen an derselben Stelle; sie schliessen sich gegenseitig aus
    // (Battles nur im Rennen, Hotlaps nur in der Quali).
    Battles {
        objectName: "battles"
        x: root.lx("battles", width, Math.round((root.width - width) / 2))
        y: root.ly("battles", height, root.height - height - 28)
        z: root.lz("battles", 40)
    }

    Hotlaps {
        objectName: "hotlaps"
        x: root.lx("hotlaps", width, Math.round((root.width - width) / 2))
        y: root.ly("hotlaps", height, root.height - height - 28)
        z: root.lz("hotlaps", 40)
    }

    LowerThird {
        objectName: "lowerthird"
        x: (root.width - width) / 2
        baseY: root.ly("lowerthird", height, root.height - 380 - height)
        z: root.lz("lowerthird", 41)
    }

    DangerZone {
        objectName: "danger"
        x: root.lx("danger", width, Math.round((root.width - width) / 2))
        baseY: root.ly("danger", height, root.height - 40 - height)
        z: root.lz("danger", 46)
    }

    // ── Unten links: Onboard, darueber die Pit-Projektion ───────────────────
    Onboard {
        id: onboard
        objectName: "onboard"
        x: root.lx("onboard", width, 24)
        y: root.ly("onboard", height, root.height - height - 28)
        z: root.lz("onboard", 44)
    }

    PitProjection {
        objectName: "pitproj"
        x: root.lx("pitproj", width, 24)
        // Im Web liegen beide auf 24 px ueber dem unteren Rand und ueberlappen
        // sich dort. Hier weicht die Projektion nach oben aus, wenn das Onboard
        // steht - sichtbar sind sie ohnehin selten gleichzeitig. Das Ausweichen
        // steckt im Rueckfallwert: sobald die Projektion einen eigenen Eintrag
        // hat, gilt der Platz, den du ihr gegeben hast.
        y: root.ly("pitproj", height,
                   root.height - height - (onboard.visible ? 28 + onboard.height + 10 : 24))
        z: root.lz("pitproj", 44)
    }

    // ── Unten rechts: Boxenstopps, darunter der WM-Stand ────────────────────
    PitCards {
        objectName: "pitcards"
        x: root.lx("pitcards", width, root.width - width - 24)
        y: root.ly("pitcards", height, root.height - height - 28)
        z: root.lz("pitcards", 45)
    }

    Championship {
        objectName: "champ"
        x: root.lx("champ", width, root.width - width - 24)
        y: root.ly("champ", height, root.height - height - 40)
        z: root.lz("champ", 45)
    }

    // ── Chart: legt sich mit abgedunkeltem Grund ueber alles ────────────────
    Charts {
        anchors.fill: parent
        z: 88
    }

    // ── Platzhalter fuer Bausteine, die gerade nichts anzeigen ──────────────
    // Die Vorschau speist erfundene Renndaten ein, aber manches erscheint darin
    // nie: die Hotlap-Boxen und die Gefahrenzone gibt es nur in der Quali, andere
    // Bausteine haengen an Ereignissen. Ein unsichtbarer Baustein ist 0 px gross
    // und damit unerreichbar - deshalb hier ein greifbarer Kasten.
    //
    // ⚠ Der Repeater steht ABSICHTLICH direkt in root: seine Elemente werden
    // dadurch Kinder von root und damit von teilUnter() gefunden. Laegen sie in
    // der Bearbeiten-Flaeche, muesste die Trefferpruefung zwei Orte durchsuchen.
    // Position und Ebene kommen vom echten Baustein - der hat seine Koordinaten
    // auch dann, wenn er nichts anzeigt. So stehen die Standardpositionen nur an
    // einer Stelle.
    Repeater {
        model: ["tower", "trackmap", "racemsg", "flbanner", "lights", "battles",
                "hotlaps", "lowerthird", "danger", "onboard", "pitproj",
                "pitcards", "champ"]

        Item {
            id: platz
            required property string modelData
            readonly property var echt: root.echtes(modelData)

            objectName: "ph:" + modelData
            visible: Hud.layoutEdit && (!echt || !echt.visible
                                        || echt.width < 4 || echt.height < 4)
            x: echt ? echt.x : 0
            y: echt ? echt.y : 0
            width: root.teilGroessen[modelData][0]
            height: root.teilGroessen[modelData][1]
            z: echt ? echt.z : 40

            Rectangle {
                anchors.fill: parent
                radius: Theme.panelRadius
                color: Qt.rgba(0, 0, 0, 0.55)
                border { width: 2; color: Qt.rgba(1, 1, 1, 0.35) }

                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: platz.modelData
                        color: Theme.textMain
                        font { family: Theme.display; pixelSize: 22; weight: Font.Bold
                               capitalization: Font.AllUppercase; letterSpacing: 2 }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "zeigt gerade nichts an"
                        color: Theme.textMuted
                        font { family: Theme.sans; pixelSize: 12 }
                    }
                }
            }
        }
    }

    // ── Layout bearbeiten: Bausteine mit der Maus ziehen ────────────────────
    // Waehrend des Ziehens geht die neue Lage nur LOKAL ins Overlay
    // (Kers.layoutLive) - so folgt der Baustein sofort der Maus, ohne dass bei
    // jedem Mausschritt eine Anfrage zum Server geht. Erst beim Loslassen wird
    // gespeichert (Kers.layoutSpeichern). Begruendung dazu in bridge.py.
    Item {
        id: layoutEditor
        anchors.fill: parent
        visible: Hud.layoutEdit
        z: 9998

        property var griff: null        // gerade gepackter Baustein
        property var unterMaus: null    // nur zum Hervorheben
        property real packX: 0          // Abstand Mauszeiger zur Bausteinecke
        property real packY: 0
        property string ecke: ""

        function karte(name, e, dx, dy, z) {
            const neu = {};
            const alt = Kers.settings.layout || {};
            for (const k in alt) neu[k] = alt[k];
            neu[name] = { ecke: e, dx: dx, dy: dy, z: Math.round(z) };
            return neu;
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: layoutEditor.griff ? Qt.ClosedHandCursor
                       : (layoutEditor.unterMaus ? Qt.OpenHandCursor : Qt.ArrowCursor)

            onPositionChanged: (m) => {
                if (!layoutEditor.griff) {
                    layoutEditor.unterMaus = root.teilUnter(m.x, m.y);
                    return;
                }
                const t = layoutEditor.griff;
                // In der Szene halten - sonst zieht man einen Baustein aus dem
                // Bild und kommt ohne die Regler in /settings nicht mehr heran.
                const nx = Math.max(0, Math.min(root.width - t.width, m.x - layoutEditor.packX));
                const ny = Math.max(0, Math.min(root.height - t.height, m.y - layoutEditor.packY));
                Kers.layoutLive(layoutEditor.karte(
                    root.schluessel(t), layoutEditor.ecke,
                    root.dxAus(layoutEditor.ecke, nx, t.width),
                    root.dyAus(layoutEditor.ecke, ny, t.height), t.z));
            }

            onPressed: (m) => {
                const t = root.teilUnter(m.x, m.y);
                if (!t) return;
                layoutEditor.griff = t;
                layoutEditor.unterMaus = t;
                layoutEditor.packX = m.x - t.x;
                layoutEditor.packY = m.y - t.y;
                // Wer schon einen Ankerpunkt hat, behaelt ihn - sonst spraenge die
                // Bezugsecke beim kleinsten Ziehen um. Nur beim ERSTEN Verschieben
                // wird der naechstgelegene genommen.
                const vorhanden = root.teil(root.schluessel(t));
                layoutEditor.ecke = vorhanden ? (vorhanden.ecke || "tl")
                                              : root.eckeNah(t.x, t.y, t.width, t.height);
            }

            onReleased: {
                const t = layoutEditor.griff;
                if (t) {
                    Kers.layoutSpeichern(layoutEditor.karte(
                        root.schluessel(t), layoutEditor.ecke,
                        root.dxAus(layoutEditor.ecke, t.x, t.width),
                        root.dyAus(layoutEditor.ecke, t.y, t.height), t.z));
                }
                layoutEditor.griff = null;
            }
        }

        // Hervorhebung des Bausteins unter der Maus
        Rectangle {
            visible: layoutEditor.unterMaus !== null
            x: layoutEditor.unterMaus ? layoutEditor.unterMaus.x : 0
            y: layoutEditor.unterMaus ? layoutEditor.unterMaus.y : 0
            width: layoutEditor.unterMaus ? layoutEditor.unterMaus.width : 0
            height: layoutEditor.unterMaus ? layoutEditor.unterMaus.height : 0
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
            border { width: 2; color: Theme.accent }
            radius: Theme.panelRadius

            Rectangle {
                anchors { left: parent.left; bottom: parent.top; bottomMargin: 2 }
                width: nameText.implicitWidth + 12
                height: nameText.implicitHeight + 6
                radius: 4
                color: Theme.accent
                Text {
                    id: nameText
                    anchors.centerIn: parent
                    text: root.schluessel(layoutEditor.unterMaus)
                    color: "#ffffff"
                    font { family: Theme.sans; pixelSize: 13; weight: Font.Bold }
                }
            }
        }

        // Hinweiszeile oben mittig
        Rectangle {
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top
                      topMargin: 12 }
            width: hinweis.implicitWidth + 28
            height: hinweis.implicitHeight + 16
            radius: Theme.panelRadius
            color: Theme.panelBg
            border { width: 1; color: Theme.accent }
            Text {
                id: hinweis
                anchors.centerIn: parent
                text: "Layout bearbeiten — Bausteine ziehen. Beenden im Schaltbrett."
                color: Theme.textMain
                font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold }
            }
        }
    }

    // Der Bearbeiten-Rahmen liegt ueber allem, ist aber nur sichtbar, wenn das
    // HUD entsperrt ist. Gesperrt gehen ohnehin alle Klicks durchs Fenster hindurch.
    // Beim Layout-Bearbeiten ist er aus: dort geht es um die Bausteine, nicht um
    // das Fenster, und sein Rahmen laege nur stoerend darueber.
    EditFrame {
        anchors.fill: parent
        visible: !Hud.locked && !Hud.layoutEdit
        z: 9999
    }
}
