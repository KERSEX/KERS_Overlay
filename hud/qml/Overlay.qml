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

    // ── Timing Tower: oben links ────────────────────────────────────────────
    Tower {
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
        x: root.lx("racemsg", width, Math.round((root.width - width) / 2))
        baseY: root.ly("racemsg", height, 22)
        z: root.lz("racemsg", 46)
    }

    FastestLapBanner {
        x: root.lx("flbanner", width, Math.round((root.width - width) / 2))
        baseY: root.ly("flbanner", height, 84)
        z: root.lz("flbanner", 47)
    }

    StartLights {
        x: root.lx("lights", width, Math.round((root.width - width) / 2))
        y: root.ly("lights", height, root.height * 0.12)
        z: root.lz("lights", 80)
    }

    // ── Unten mittig: Battle-Boxen im Rennen, Hotlap-Boxen in der Quali ─────
    // Beide sitzen an derselben Stelle; sie schliessen sich gegenseitig aus
    // (Battles nur im Rennen, Hotlaps nur in der Quali).
    Battles {
        x: root.lx("battles", width, Math.round((root.width - width) / 2))
        y: root.ly("battles", height, root.height - height - 28)
        z: root.lz("battles", 40)
    }

    Hotlaps {
        x: root.lx("hotlaps", width, Math.round((root.width - width) / 2))
        y: root.ly("hotlaps", height, root.height - height - 28)
        z: root.lz("hotlaps", 40)
    }

    LowerThird {
        x: (root.width - width) / 2
        baseY: root.ly("lowerthird", height, root.height - 380 - height)
        z: root.lz("lowerthird", 41)
    }

    DangerZone {
        x: root.lx("danger", width, Math.round((root.width - width) / 2))
        baseY: root.ly("danger", height, root.height - 40 - height)
        z: root.lz("danger", 46)
    }

    // ── Unten links: Onboard, darueber die Pit-Projektion ───────────────────
    Onboard {
        id: onboard
        x: root.lx("onboard", width, 24)
        y: root.ly("onboard", height, root.height - height - 28)
        z: root.lz("onboard", 44)
    }

    PitProjection {
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
        x: root.lx("pitcards", width, root.width - width - 24)
        y: root.ly("pitcards", height, root.height - height - 28)
        z: root.lz("pitcards", 45)
    }

    Championship {
        x: root.lx("champ", width, root.width - width - 24)
        y: root.ly("champ", height, root.height - height - 40)
        z: root.lz("champ", 45)
    }

    // ── Chart: legt sich mit abgedunkeltem Grund ueber alles ────────────────
    Charts {
        anchors.fill: parent
        z: 88
    }

    // Der Bearbeiten-Rahmen liegt ueber allem, ist aber nur sichtbar, wenn das
    // HUD entsperrt ist. Gesperrt gehen ohnehin alle Klicks durchs Fenster hindurch.
    EditFrame {
        anchors.fill: parent
        visible: !Hud.locked
        z: 9999
    }
}
