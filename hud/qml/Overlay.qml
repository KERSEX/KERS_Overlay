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
    //
    // ⚠ Der Rueckfall gilt FELDWEISE, nicht nur fuer den ganzen Eintrag. Ein
    // Eintrag darf unvollstaendig sein: {"z": 55} ohne dx/dy heisst "Ebene
    // eigen, Position weiter wie eingebaut". Genau das schreibt die
    // Ebenen-Liste in /settings - sie sortiert nur die Reihenfolge um, und
    // wuerde sie dabei dx/dy mitschreiben, waeren die vier beweglichen
    // Bausteine mit einem Klick auf feste Zahlen genagelt.
    function teil(name) {
        const L = Kers.settings.layout;
        return (L && L[name]) ? L[name] : null;
    }
    /** Ist das Feld im Eintrag wirklich gesetzt? `undefined` und `null` heissen
     *  beide "nicht gesetzt" - aus JSON kommt null, aus QML/JS undefined. */
    function hat(t, feld) {
        return !!t && t[feld] !== undefined && t[feld] !== null;
    }
    function lx(name, w, standard) {
        const t = teil(name);
        if (!root.hat(t, "dx")) return standard;
        const e = t.ecke || "tl";
        if (e === "tl" || e === "lc" || e === "bl") return t.dx;
        if (e === "tr" || e === "rc" || e === "br") return root.width - w - t.dx;
        return Math.round((root.width - w) / 2) + t.dx;      // tc / cc / bc
    }
    function ly(name, h, standard) {
        const t = teil(name);
        if (!root.hat(t, "dy")) return standard;
        const e = t.ecke || "tl";
        if (e === "tl" || e === "tc" || e === "tr") return t.dy;
        if (e === "bl" || e === "bc" || e === "br") return root.height - h - t.dy;
        return Math.round((root.height - h) / 2) + t.dy;      // lc / cc / rc
    }
    function lz(name, standard) {
        const t = teil(name);
        return root.hat(t, "z") ? t.z : standard;
    }
    /** Groessenfaktor eines Bausteins. 1 = wie einprogrammiert.
     *
     *  ⚠ Ein Eintrag OHNE `groesse` gilt als 1. Das ist der Grund, warum
     *  Einstellungsdateien aus 0.2.0 unveraendert weiterlaufen: dort steht das
     *  Feld nicht drin, und es soll auch nicht nachgetragen werden. */
    function ls(name) {
        const t = teil(name);
        const g = t ? t.groesse : undefined;
        return (g === undefined || g === null || !(g > 0)) ? 1 : g;
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

    /** Das y, das die LAGE bestimmt - nicht das gerade sichtbare.
     *
     *  ⚠ Vier Bausteine (Meldungs-Banner, Fastest-Lap-Banner, Lower-Third,
     *  Gefahrenzone) werden ueber `baseY` positioniert und animieren ihr `y`
     *  davon weg: beim Ein- und Ausblenden um 14 bis 18 px. Liest man beim
     *  Ziehen `y` und schreibt es als `baseY` zurueck, wird dieser Versatz
     *  jedes Mal erneut aufgeschlagen - der Baustein sitzt dann neben dem
     *  Mauszeiger und wandert bei jedem Anfassen weiter. */
    function lageY(item) {
        return (item && item.baseY !== undefined) ? item.baseY : (item ? item.y : 0);
    }

    /** Mit WELCHER Groesse zeichnet lx/ly diesen Baustein gleich?
     *
     *  ⚠ Das ist der Kern der Umrechnung. dxAus/dyAus muessen mit derselben
     *  Groesse rechnen, mit der lx/ly nachher zeichnen - sonst verschiebt sich
     *  der Baustein um die Differenz. Bei unten verankerten nach unten, bei
     *  mittig verankerten um die Haelfte.
     *
     *  Zwei Quellen fuer eine Abweichung, beide real aufgetreten:
     *    1. Die Groesse beim Anfassen wird eingefroren (damit der Baustein
     *       waehrend des Ziehens nicht unter der Maus wegwandert, wenn sein
     *       Inhalt waechst) - gezeichnet wird aber mit der aktuellen.
     *    2. Man fasst einen PLATZHALTER an. Der traegt eine geschaetzte Groesse
     *       aus teilGroessen, gezeichnet wird spaeter der echte Baustein.
     *
     *  Deshalb hier immer die Groesse des ECHTEN Bausteins nehmen, und nur wenn
     *  der gerade nichts anzeigt die des Platzhalters. */
    /** Die Flaeche, die ein Baustein wirklich einnimmt.
     *
     *  ⚠ Seit dem Groessenfaktor ist `width` NICHT mehr die sichtbare Breite.
     *  Qt skaliert den gezeichneten Baum, nicht die Geometrie: die Kinder eines
     *  Bausteins lesen weiter seine unskalierte Breite (und muessen das auch,
     *  sonst ginge der Faktor bei jedem `parent.width` ein zweites Mal ein).
     *  Nach AUSSEN zaehlt darum width * scale - fuer die Lage, die
     *  Trefferpruefung, die Marken und die Umrechnung beim Ziehen.
     *
     *  Tower und Lower-Third rechnen ihren Faktor selbst in ihre Masse ein und
     *  lassen `scale` auf 1; fuer die kommt hier dasselbe heraus. */
    function fbW(item) { return item ? item.width * item.scale : 0; }
    function fbH(item) { return item ? item.height * item.scale : 0; }

    function zielGroesse(item) {
        const e = root.echtes(root.schluessel(item));
        if (e && root.fbW(e) > 4 && root.fbH(e) > 4)
            return [root.fbW(e), root.fbH(e)];
        return [root.fbW(item), root.fbH(item)];
    }

    /** Alle Schluessel - Reihenfolge egal, gebraucht fuer Platzhalter und Marken. */
    readonly property var teilNamen: [
        "tower", "trackmap", "racemsg", "flbanner", "lights", "battles",
        "hotlaps", "lowerthird", "danger", "onboard", "pitproj",
        "pitcards", "champ"]

    /** Der Platzhalter zu einem Schluessel. Gegenstueck zu echtes(). */
    function platzhalter(name) {
        for (let i = 0; i < root.children.length; i++)
            if (root.children[i].objectName === "ph:" + name)
                return root.children[i];
        return null;
    }

    /** Hat der ECHTE Baustein hinter diesem Element schon eine Groesse?
     *  Nein heisst: er zeigt noch nie etwas an, man haelt seinen Platzhalter. */
    function hatGroesse(item) {
        const e = root.echtes(root.schluessel(item));
        return !!(e && root.fbW(e) > 4 && root.fbH(e) > 4);
    }

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
        let treffer = null, bester = -1;
        for (let i = 0; i < root.children.length; i++) {
            const c = root.children[i];
            if (!c.visible || !c.objectName) continue;
            // Sichtbare Flaeche, nicht die Geometrie - siehe fbW().
            if (px < c.x || px > c.x + root.fbW(c)
                || py < c.y || py > c.y + root.fbH(c)) continue;
            // ⚠ Bei gleicher Ebene gewinnt der ECHTE Baustein gegen einen
            // Platzhalter. Battle- und Hotlap-Boxen liegen auf demselben Platz
            // und derselben Ebene 40 und schliessen sich gegenseitig aus - ohne
            // diesen Vorrang packte man im Rennen immer den Platzhalter der
            // Hotlap-Boxen, der ueber den sichtbaren Battle-Boxen liegt.
            const rang = c.z * 2 + (c.objectName.indexOf("ph:") === 0 ? 0 : 1);
            if (rang >= bester) { bester = rang; treffer = c; }
        }
        return treffer;
    }

    // ── Timing Tower: oben links ────────────────────────────────────────────
    Tower {
        id: towerTeil
        objectName: "tower"
        // Der linke Abstand existiert NUR, um die Strafen-Pillen aufzufangen, die
        // links aus der Zeile herausragen (im CSS: padding-left am body, dort 72,
        // hier auf Wunsch um ein Drittel verringert auf 48).
        //
        // Stehen die Pillen rechts (Setting penside), braucht es links gar keine
        // Reserve mehr - dann reicht derselbe Randabstand wie bei der Trackmap.
        // Der Tower rueckt damit von selbst nach links, sobald du umstellst, und
        // wieder zurueck, wenn du es rueckgaengig machst.
        x: root.lx("tower", root.fbW(towerTeil),
                   Kers.settings.penSide === "right" ? 12 : 48)
        y: root.ly("tower", root.fbH(towerTeil), 10)
        stageHeight: root.height
        z: root.lz("tower", 30)
        groesse: root.ls("tower")     // rechnet er selbst ein, siehe Tower.qml
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
        transformOrigin: Item.TopLeft
        scale: root.ls("trackmap")
        // ⚠ Ueberall die sichtbare Flaeche (fbW/fbH) statt width/height - sonst
        // rechnet der Rueckfall mit der unskalierten Groesse und die Karte
        // ragt bei einem Faktor ueber 1 aus dem Bild.
        x: root.lx("trackmap", root.fbW(trackmap),
                   corner === "bl" ? gap
                   : (corner === "tc" || corner === "bc")
                     ? Math.round((root.width - root.fbW(trackmap)) / 2)
                   : root.width - root.fbW(trackmap) - gap)
        y: root.ly("trackmap", root.fbH(trackmap),
                   (corner === "tc" || corner === "tr") ? gap
                   : corner === "rc"
                     ? Math.round((root.height - root.fbH(trackmap)) / 2)
                   : root.height - root.fbH(trackmap) - gap)
    }

    // ── Meldungen und Banner: oben mittig ───────────────────────────────────
    // ⚠ Die frueheren `anchors.horizontalCenter` sind hier ueberall gegen ein
    // gerechnetes x getauscht. Ein Anker gewinnt immer gegen x - beides
    // gleichzeitig geht nicht, und abschalten laesst er sich nicht verlaesslich
    // (siehe die Warnung bei der Trackmap). Der Rueckfallwert bildet exakt das
    // ab, was der Anker vorher tat.
    RaceMessage {
        id: racemsgTeil
        objectName: "racemsg"
        transformOrigin: Item.TopLeft
        scale: root.ls("racemsg")
        x: root.lx("racemsg", root.fbW(racemsgTeil),
                   Math.round((root.width - root.fbW(racemsgTeil)) / 2))
        baseY: root.ly("racemsg", root.fbH(racemsgTeil), 22)
        z: root.lz("racemsg", 46)
    }

    FastestLapBanner {
        id: flbannerTeil
        objectName: "flbanner"
        transformOrigin: Item.TopLeft
        scale: root.ls("flbanner")
        x: root.lx("flbanner", root.fbW(flbannerTeil),
                   Math.round((root.width - root.fbW(flbannerTeil)) / 2))
        baseY: root.ly("flbanner", root.fbH(flbannerTeil), 84)
        z: root.lz("flbanner", 47)
    }

    StartLights {
        id: lightsTeil
        objectName: "lights"
        transformOrigin: Item.TopLeft
        scale: root.ls("lights")
        x: root.lx("lights", root.fbW(lightsTeil),
                   Math.round((root.width - root.fbW(lightsTeil)) / 2))
        y: root.ly("lights", root.fbH(lightsTeil), root.height * 0.12)
        z: root.lz("lights", 80)
    }

    // ── Unten mittig: Battle-Boxen im Rennen, Hotlap-Boxen in der Quali ─────
    // Beide sitzen an derselben Stelle; sie schliessen sich gegenseitig aus
    // (Battles nur im Rennen, Hotlaps nur in der Quali).
    Battles {
        id: battlesTeil
        objectName: "battles"
        transformOrigin: Item.TopLeft
        scale: root.ls("battles")
        x: root.lx("battles", root.fbW(battlesTeil),
                   Math.round((root.width - root.fbW(battlesTeil)) / 2))
        y: root.ly("battles", root.fbH(battlesTeil),
                   root.height - root.fbH(battlesTeil) - 28)
        z: root.lz("battles", 40)
    }

    Hotlaps {
        id: hotlapsTeil
        objectName: "hotlaps"
        transformOrigin: Item.TopLeft
        scale: root.ls("hotlaps")
        x: root.lx("hotlaps", root.fbW(hotlapsTeil),
                   Math.round((root.width - root.fbW(hotlapsTeil)) / 2))
        y: root.ly("hotlaps", root.fbH(hotlapsTeil),
                   root.height - root.fbH(hotlapsTeil) - 28)
        z: root.lz("hotlaps", 40)
    }

    LowerThird {
        id: lowerthirdTeil
        objectName: "lowerthird"
        // ⚠ Ging bis 0.2.0 NICHT durch lx: das Lower-Third liess sich waagerecht
        // ziehen, sprang aber beim Loslassen zurueck in die Mitte.
        groesse: root.ls("lowerthird")   // rechnet er selbst ein, s. LowerThird.qml
        x: root.lx("lowerthird", root.fbW(lowerthirdTeil),
                   Math.round((root.width - root.fbW(lowerthirdTeil)) / 2))
        baseY: root.ly("lowerthird", root.fbH(lowerthirdTeil),
                       root.height - 380 - root.fbH(lowerthirdTeil))
        z: root.lz("lowerthird", 41)
    }

    DangerZone {
        id: dangerTeil
        objectName: "danger"
        transformOrigin: Item.TopLeft
        scale: root.ls("danger")
        x: root.lx("danger", root.fbW(dangerTeil),
                   Math.round((root.width - root.fbW(dangerTeil)) / 2))
        baseY: root.ly("danger", root.fbH(dangerTeil),
                       root.height - 40 - root.fbH(dangerTeil))
        z: root.lz("danger", 46)
    }

    // ── Unten links: Onboard, darueber die Pit-Projektion ───────────────────
    Onboard {
        id: onboard
        objectName: "onboard"
        transformOrigin: Item.TopLeft
        scale: root.ls("onboard")
        x: root.lx("onboard", root.fbW(onboard), 24)
        y: root.ly("onboard", root.fbH(onboard),
                   root.height - root.fbH(onboard) - 28)
        z: root.lz("onboard", 44)
    }

    PitProjection {
        id: pitprojTeil
        objectName: "pitproj"
        transformOrigin: Item.TopLeft
        scale: root.ls("pitproj")
        x: root.lx("pitproj", root.fbW(pitprojTeil), 24)
        // Im Web liegen beide auf 24 px ueber dem unteren Rand und ueberlappen
        // sich dort. Hier weicht die Projektion nach oben aus, wenn das Onboard
        // steht - sichtbar sind sie ohnehin selten gleichzeitig. Das Ausweichen
        // steckt im Rueckfallwert: sobald die Projektion einen eigenen Eintrag
        // hat, gilt der Platz, den du ihr gegeben hast.
        //
        // ⚠ Auch hier die SICHTBARE Hoehe des Onboards: vergroessert man es,
        // muss die Projektion entsprechend weiter nach oben ausweichen.
        y: root.ly("pitproj", root.fbH(pitprojTeil),
                   root.height - root.fbH(pitprojTeil)
                   - (onboard.visible ? 28 + root.fbH(onboard) + 10 : 24))
        z: root.lz("pitproj", 44)
    }

    // ── Unten rechts: Boxenstopps, darunter der WM-Stand ────────────────────
    PitCards {
        id: pitcardsTeil
        objectName: "pitcards"
        transformOrigin: Item.TopLeft
        scale: root.ls("pitcards")
        x: root.lx("pitcards", root.fbW(pitcardsTeil),
                   root.width - root.fbW(pitcardsTeil) - 24)
        y: root.ly("pitcards", root.fbH(pitcardsTeil),
                   root.height - root.fbH(pitcardsTeil) - 28)
        z: root.lz("pitcards", 45)
    }

    Championship {
        id: champTeil
        objectName: "champ"
        transformOrigin: Item.TopLeft
        scale: root.ls("champ")
        x: root.lx("champ", root.fbW(champTeil),
                   root.width - root.fbW(champTeil) - 24)
        y: root.ly("champ", root.fbH(champTeil),
                   root.height - root.fbH(champTeil) - 40)
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
        model: root.teilNamen

        Item {
            id: platz
            required property string modelData
            readonly property var echt: root.echtes(modelData)

            objectName: "ph:" + modelData
            visible: Hud.layoutEdit && (!echt || !echt.visible
                                        || root.fbW(echt) < 4 || root.fbH(echt) < 4)
            // ⚠ In die Szene zwingen. Ein Baustein, der nichts anzeigt, ist
            // 0 px gross - seine unten verankerte Lage ist damit buendig mit der
            // Unterkante, und ein 210 px hoher Platzhalter haengt zu drei
            // Vierteln aus dem Bild. Genau die Hotlap-Boxen waren so im Rennen
            // praktisch nicht zu greifen.
            x: Math.max(0, Math.min(root.width - width, echt ? echt.x : 0))
            y: Math.max(0, Math.min(root.height - height, root.lageY(echt)))
            // ⚠ Die Schaetzung aus teilGroessen NUR, wenn der echte Baustein gar
            // keine Groesse hat. Hat er eine (er zeigt nur nichts an, ist aber
            // ausgemessen), gilt seine - sonst faengt man den Platzhalter in
            // einer Groesse an, in der der Baustein nachher nie gezeichnet wird.
            // Auch hier die sichtbare Flaeche - ein vergroesserter Baustein soll
            // seinen Platzhalter in genau der Groesse hinterlassen.
            width: (echt && root.fbW(echt) > 4) ? root.fbW(echt)
                                                : root.teilGroessen[modelData][0]
                                                  * root.ls(modelData)
            height: (echt && root.fbH(echt) > 4) ? root.fbH(echt)
                                                 : root.teilGroessen[modelData][1]
                                                   * root.ls(modelData)
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
        property string ecke: ""
        // ⚠ Alles Folgende wird beim PACKEN eingefroren und waehrend des Ziehens
        // nicht mehr aus dem Baustein gelesen. Grund: die Vorschau speist ein
        // laufendes Rennen ein, die Inhalte aendern staendig ihre Groesse (Boxen
        // kommen und gehen, der Tower waechst). Rechnete man live mit der
        // aktuellen Groesse, wanderte ein unten oder mittig verankerter Baustein
        // unter der Maus weg.
        property real startX: 0         // Lage beim Packen
        property real startY: 0
        property real pressX: 0         // Mausposition beim Packen
        property real pressY: 0
        property real griffW: 0         // Groesse beim Packen
        property real griffH: 0
        property real letztX: 0         // zuletzt gesetzte Lage - die wird gespeichert
        property real letztY: 0

        // ── Groesse ziehen ──────────────────────────────────────────────────
        readonly property int anfasser: 16   // Kantenlaenge der Ecken-Quadrate
        property string modus: "lage"        // "lage" = verschieben, "groesse"
        property real startFaktor: 1
        property real letztFaktor: 1
        property int richtungX: 1            // +1 = nach rechts groesser
        property int richtungY: 1

        /** Liegt der Punkt auf einem Ecken-Anfasser des Bausteins?
         *  Rueckgabe: null oder die Zugrichtung, in der "groesser" liegt. */
        function anfasserUnter(t, px, py) {
            if (!t) return null;
            const w = root.fbW(t), h = root.fbH(t), a = layoutEditor.anfasser;
            const y0 = root.lageY(t);
            const links = px <= t.x + a, rechts = px >= t.x + w - a;
            const oben = py <= y0 + a, unten = py >= y0 + h - a;
            if (!(links || rechts) || !(oben || unten)) return null;
            return { x: links ? -1 : 1, y: oben ? -1 : 1 };
        }

        // ⚠ Der Eintrag wird ERSETZT, nicht ergaenzt. Alles, was nicht hier
        // steht, ist danach weg - deshalb muss `groesse` mit. Ohne diese Zeile
        // haette ein Verschieben die eingestellte Groesse stillschweigend auf 1
        // zurueckgesetzt, und zwar ohne Fehlermeldung irgendwo.
        /** Der Layout-Eintrag beim Groessenziehen.
         *
         *  ⚠ Die gegenueberliegende Ecke bleibt liegen - der Baustein waechst
         *  aus der Ecke heraus, die man haelt. Ohne das richtet er sich nach
         *  seinem ANKERPUNKT, und der ist bei sieben der dreizehn Bausteine
         *  mittig (tc/bc/cc): dort waere er nach beiden Seiten gleichzeitig
         *  gewachsen, also "aus der Mitte heraus".
         *
         *  Gerechnet wird nicht an der Maus, sondern an der eingefrorenen
         *  Flaeche mal dem Verhaeltnis der Faktoren. So bleibt die feste Ecke
         *  wirklich stehen, statt um Rundungsreste zu wandern. */
        function groesseKarte(t, f) {
            const k = layoutEditor.startFaktor > 0
                      ? f / layoutEditor.startFaktor : 1;
            const w = layoutEditor.griffW * k;
            const h = layoutEditor.griffH * k;
            let nx = layoutEditor.richtungX > 0
                     ? layoutEditor.startX
                     : layoutEditor.startX + layoutEditor.griffW - w;
            let ny = layoutEditor.richtungY > 0
                     ? layoutEditor.startY
                     : layoutEditor.startY + layoutEditor.griffH - h;
            // ⚠ In der Szene halten, wie beim Verschieben. Zieht man einen
            // Baustein an der linken oberen Ecke auf, waechst er nach links -
            // ohne Grenze bis aus dem Bild heraus (gemessen: x = -60). Am Rand
            // gibt dann die feste Ecke nach; das ist das kleinere Uebel,
            // verglichen mit einem Baustein, den man nicht mehr sieht.
            nx = Math.max(0, Math.min(root.width - w, nx));
            ny = Math.max(0, Math.min(root.height - h, ny));
            return layoutEditor.karte(
                root.schluessel(t), layoutEditor.ecke,
                root.dxAus(layoutEditor.ecke, nx, w),
                root.dyAus(layoutEditor.ecke, ny, h), t.z, f);
        }

        function karte(name, e, dx, dy, z, groesse) {
            const neu = {};
            const alt = Kers.settings.layout || {};
            for (const k in alt) neu[k] = alt[k];
            neu[name] = { ecke: e, dx: dx, dy: dy, z: Math.round(z),
                          groesse: (groesse === undefined) ? root.ls(name)
                                                           : groesse };
            return neu;
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: {
                if (layoutEditor.griff)
                    return layoutEditor.modus === "groesse" ? Qt.SizeFDiagCursor
                                                            : Qt.ClosedHandCursor;
                if (!layoutEditor.unterMaus) return Qt.ArrowCursor;
                const a = layoutEditor.anfasserUnter(layoutEditor.unterMaus,
                                                     mouseX, mouseY);
                if (!a) return Qt.OpenHandCursor;
                return a.x === a.y ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor;
            }

            onPositionChanged: (m) => {
                if (!layoutEditor.griff) {
                    layoutEditor.unterMaus = root.teilUnter(m.x, m.y);
                    return;
                }
                const t = layoutEditor.griff;

                // ── Groesse ziehen ──────────────────────────────────────────
                // Die Lage wandert mit: die gegenueberliegende Ecke soll
                // liegen bleiben, siehe groesseKarte().
                if (layoutEditor.modus === "groesse") {
                    const dw = (m.x - layoutEditor.pressX) * layoutEditor.richtungX;
                    const dh = (m.y - layoutEditor.pressY) * layoutEditor.richtungY;
                    // Mittel aus beiden Achsen, jede an der eigenen Kantenlaenge
                    // gemessen - so zieht sich ein flacher Balken nicht anders an
                    // als ein hoher Kasten.
                    const rel = ((layoutEditor.griffW > 0 ? dw / layoutEditor.griffW : 0)
                               + (layoutEditor.griffH > 0 ? dh / layoutEditor.griffH : 0)) / 2;
                    layoutEditor.letztFaktor = Math.max(
                        0.5, Math.min(2, layoutEditor.startFaktor * (1 + rel)));
                    Kers.layoutLive(layoutEditor.groesseKarte(
                        t, layoutEditor.letztFaktor));
                    return;
                }
                // Reine Verschiebung ab dem Packpunkt. In der Szene halten -
                // sonst zieht man einen Baustein aus dem Bild und kommt ohne die
                // Regler in /settings nicht mehr heran.
                layoutEditor.letztX = Math.max(0, Math.min(root.width - layoutEditor.griffW,
                                     layoutEditor.startX + (m.x - layoutEditor.pressX)));
                layoutEditor.letztY = Math.max(0, Math.min(root.height - layoutEditor.griffH,
                                     layoutEditor.startY + (m.y - layoutEditor.pressY)));
                // ⚠ Fuer die Umrechnung die ZEICHEN-Groesse, nicht die
                // eingefrorene: siehe zielGroesse(). Die eingefrorene bleibt
                // fuer die Bewegung selbst zustaendig (oben), damit der
                // Baustein nicht unter der Maus wegwandert.
                const g = root.zielGroesse(t);
                Kers.layoutLive(layoutEditor.karte(
                    root.schluessel(t), layoutEditor.ecke,
                    root.dxAus(layoutEditor.ecke, layoutEditor.letztX, g[0]),
                    root.dyAus(layoutEditor.ecke, layoutEditor.letztY, g[1]),
                    t.z));
            }

            onPressed: (m) => {
                const t = root.teilUnter(m.x, m.y);
                if (!t) return;
                layoutEditor.griff = t;
                layoutEditor.unterMaus = t;
                // ⚠ lageY statt y: bei den vier Bausteinen mit baseY ist das
                // sichtbare y um die Ein-/Ausblend-Animation verschoben. Naehme
                // man das, saesse der Baustein nach dem Loslassen daneben.
                layoutEditor.startX = t.x;
                layoutEditor.startY = root.lageY(t);
                layoutEditor.pressX = m.x;
                layoutEditor.pressY = m.y;
                // ⚠ Die SICHTBARE Flaeche einfrieren, nicht die Geometrie -
                // sonst begrenzt der Rand beim Ziehen auf die unskalierte
                // Breite und ein vergroesserter Baustein haengt rechts raus.
                layoutEditor.griffW = root.fbW(t);
                layoutEditor.griffH = root.fbH(t);
                layoutEditor.letztX = layoutEditor.startX;
                layoutEditor.letztY = layoutEditor.startY;
                // Wer schon einen Ankerpunkt hat, behaelt ihn - sonst spraenge die
                // Bezugsecke beim kleinsten Ziehen um. Nur beim ERSTEN Verschieben
                // wird der naechstgelegene genommen.
                // ⚠ Gefragt ist der ANKERPUNKT, nicht "gibt es einen Eintrag":
                // ein Baustein, den nur die Ebenen-Liste angefasst hat, traegt
                // bloss ein `z`. Der wurde hier sonst als "hat schon eine Ecke"
                // gelesen und landete stumm auf "tl", statt sich den
                // naechstgelegenen Ankerpunkt zu suchen.
                const vorhanden = root.teil(root.schluessel(t));
                if (root.hat(vorhanden, "ecke")) {
                    layoutEditor.ecke = vorhanden.ecke;
                } else if (!root.hatGroesse(t)) {
                    // ⚠ Baustein ohne eigene Groesse (zeigt nichts an, man hat
                    // seinen Platzhalter gepackt): jede Ecke ausser oben links
                    // rechnet die Groesse mit ein - und die kennt niemand, ehe
                    // er das erste Mal etwas anzeigt. Mit "tl" ist der
                    // gespeicherte Versatz schlicht die Koordinate der oberen
                    // linken Ecke, und der Baustein taucht spaeter genau dort
                    // auf, wo der Platzhalter stand.
                    layoutEditor.ecke = "tl";
                } else {
                    layoutEditor.ecke = root.eckeNah(t.x, root.lageY(t),
                                                     root.fbW(t), root.fbH(t));
                }

                // Anfasser gepackt? Dann Groesse statt Lage. startX/startY und
                // griffW/griffH von oben sind der Bezug: aus ihnen rechnet
                // groesseKarte() die neue Lage so, dass die gegenueberliegende
                // Ecke liegen bleibt.
                const a = layoutEditor.anfasserUnter(t, m.x, m.y);
                layoutEditor.modus = a ? "groesse" : "lage";
                if (a) {
                    layoutEditor.richtungX = a.x;
                    layoutEditor.richtungY = a.y;
                    layoutEditor.startFaktor = root.ls(root.schluessel(t));
                    layoutEditor.letztFaktor = layoutEditor.startFaktor;
                }
            }

            onReleased: {
                const t = layoutEditor.griff;
                if (t && layoutEditor.modus === "groesse") {
                    Kers.layoutSpeichern(layoutEditor.groesseKarte(
                        t, layoutEditor.letztFaktor));
                } else if (t) {
                    // Dieselbe Rechnung wie beim Ziehen - so liegt der
                    // gespeicherte Platz genau dort, wo der Baustein zuletzt stand.
                    const g = root.zielGroesse(t);
                    Kers.layoutSpeichern(layoutEditor.karte(
                        root.schluessel(t), layoutEditor.ecke,
                        root.dxAus(layoutEditor.ecke, layoutEditor.letztX, g[0]),
                        root.dyAus(layoutEditor.ecke, layoutEditor.letztY, g[1]),
                        t.z));
                }
                layoutEditor.griff = null;
                layoutEditor.modus = "lage";
            }
        }

        // Marke auf JEDEM Baustein, nicht nur dem unter der Maus.
        // ⚠ Ohne die sieht man beim Bearbeiten nur, was man gerade beruehrt -
        // ein duenner, kurzlebiger Baustein wie der Meldungs-Banner (380x50,
        // und er zeigt nur zwischendurch etwas) war so kaum zu finden.
        Repeater {
            model: root.teilNamen

            Item {
                id: marke
                required property string modelData
                readonly property var echt: root.echtes(modelData)
                readonly property var platz: root.platzhalter(modelData)
                // ⚠ Bewusst als Ausdruck ueber die Eigenschaften und NICHT als
                // Funktionsaufruf: so merkt sich QML die Abhaengigkeit von
                // visible/width/height und rechnet neu, sobald ein Baustein
                // auftaucht oder verschwindet. Ein Aufruf wuerde einmal
                // ausgewertet und die Marke bliebe am ersten Ergebnis kleben.
                // Dieselbe Wahl wie in teilUnter(): die Marke soll zeigen, was
                // man trifft.
                readonly property var ziel:
                    (echt && echt.visible
                     && root.fbW(echt) > 4 && root.fbH(echt) > 4)
                    ? echt : ((platz && platz.visible) ? platz : null)

                // Liegt die Maus auf diesem Baustein? Dann ist die Marke selbst
                // die Hervorhebung - es gibt bewusst nur EIN Rechteck und EINE
                // Beschriftung je Baustein.
                readonly property bool dran: ziel !== null
                                             && layoutEditor.unterMaus === ziel

                visible: ziel !== null
                // ⚠ Das SICHTBARE y, nicht lageY/baseY. Die Marke liegt ueber
                // dem, was man sieht; bei den vier Bausteinen mit baseY sind das
                // bis zu 18 px Unterschied, und der Rahmen sass daneben.
                // (Fuer die Ziehen-Rechnung gilt weiter lageY - siehe dort.)
                x: ziel ? ziel.x : 0
                y: ziel ? ziel.y : 0
                width: root.fbW(ziel)
                height: root.fbH(ziel)

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.panelRadius
                    color: marke.dran ? Qt.rgba(Theme.accent.r, Theme.accent.g,
                                                Theme.accent.b, 0.10)
                                      : "transparent"
                    border {
                        width: marke.dran ? 2 : 1
                        color: marke.dran ? Theme.accent : Qt.rgba(1, 1, 1, 0.45)
                    }
                }

                // Ecken-Anfasser - nur am Baustein unter der Maus, sonst waeren
                // 52 Quadrate gleichzeitig im Bild.
                Repeater {
                    model: marke.dran ? ["tl", "tr", "bl", "br"] : []

                    Rectangle {
                        required property string modelData
                        width: layoutEditor.anfasser
                        height: layoutEditor.anfasser
                        color: Theme.accent
                        border { width: 1; color: "#ffffff" }
                        x: modelData.indexOf("l") >= 0
                           ? 0 : marke.width - layoutEditor.anfasser
                        y: modelData.indexOf("t") >= 0
                           ? 0 : marke.height - layoutEditor.anfasser
                    }
                }

                // Beschriftung INNEN oben links - ausserhalb waere sie bei einem
                // Baustein am oberen Rand (Meldungs-Banner sitzt auf y=22) weg.
                Rectangle {
                    // Rueckt zur Seite, sobald die Anfasser da sind - sonst
                    // liegt das Schild unter dem Quadrat oben links.
                    anchors { left: parent.left; top: parent.top; margins: 3
                              leftMargin: marke.dran ? layoutEditor.anfasser + 5 : 3 }
                    width: markeText.implicitWidth + 10
                    height: markeText.implicitHeight + 4
                    radius: 3
                    color: marke.dran ? Theme.accent : Qt.rgba(0, 0, 0, 0.65)
                    Text {
                        id: markeText
                        anchors.centerIn: parent
                        text: marke.modelData
                        color: "#ffffff"
                        font { family: Theme.sans; pixelSize: 11; weight: Font.Bold }
                    }
                }
            }
        }

        // Hinweiszeile oben mittig - mit eigenem Fertig-Knopf.
        // ⚠ Der Knopf MUSS hier sein und nicht nur im Schaltbrett: zum Bearbeiten
        // wird das Fenster entsperrt, und dann liegt eine bildschirmgrosse
        // Klickflaeche in der Vordergrund-Schicht ueber allem - das Schaltbrett
        // ist unter ihr nicht mehr erreichbar (derselbe Grund, aus dem
        // fill_screen() in subsystems_panel.py zwingend wieder sperrt).
        Rectangle {
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top
                      topMargin: 12 }
            width: zeile.implicitWidth + 28
            height: zeile.implicitHeight + 16
            radius: Theme.panelRadius
            // Halb durchsichtig: der Balken sitzt oben mittig und damit genau
            // ueber Meldungs-Banner und Fastest-Lap-Banner. Deckend verdeckte er
            // beim Bearbeiten zwei der Bausteine, die man platzieren will.
            color: Qt.rgba(Theme.panelBg.r, Theme.panelBg.g, Theme.panelBg.b, 0.55)
            border { width: 1; color: Qt.rgba(Theme.accent.r, Theme.accent.g,
                                              Theme.accent.b, 0.7) }

            Row {
                id: zeile
                anchors.centerIn: parent
                spacing: 14

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Layout bearbeiten — Bausteine ziehen."
                    color: Theme.textMain
                    font { family: Theme.sans; pixelSize: 15; weight: Font.DemiBold }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: fertigText.implicitWidth + 24
                    height: fertigText.implicitHeight + 14
                    radius: 4
                    color: fertigMaus.pressed ? Qt.darker(Theme.accent, 1.3)
                         : (fertigMaus.containsMouse ? Qt.lighter(Theme.accent, 1.2)
                                                     : Theme.accent)
                    Text {
                        id: fertigText
                        anchors.centerIn: parent
                        text: "Fertig (Esc)"
                        color: "#ffffff"
                        font { family: Theme.sans; pixelSize: 15; weight: Font.Bold }
                    }
                    MouseArea {
                        id: fertigMaus
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Hud.layoutEdit = false
                        // Sonst bliebe die Hervorhebung auf dem Baustein
                        // haengen, ueber dem die Maus zuletzt war.
                        onEntered: layoutEditor.unterMaus = null
                    }
                }
            }
        }

        // Rueckfallebene, falls ein Baustein ueber dem Knopf liegt.
        // ⚠ Qt.ApplicationShortcut, nicht der voreingestellte Fenster-Bezug: das
        // Overlay wird bewusst ohne Fokus gezeigt (ShowWindow mit
        // SW_SHOWNOACTIVATE, damit es dem Spiel den Fokus nicht klaut), ist also
        // meist NICHT das aktive Fenster - ein Fenster-Kuerzel bekaeme nie eine
        // Taste zu sehen. Mit Anwendungs-Bezug greift Esc auch, wenn gerade das
        // Schaltbrett vorn ist. Zusaetzlich holt sich das Fenster beim
        // Einschalten den Fokus (requestActivate in qml_overlay.py).
        Shortcut {
            sequence: "Esc"
            context: Qt.ApplicationShortcut
            enabled: Hud.layoutEdit
            onActivated: Hud.layoutEdit = false
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
