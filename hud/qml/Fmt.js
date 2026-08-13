// Zahlen- und Farbformate des Overlays.
// Portierung der gleichnamigen Funktionen aus static/js/core.js und static/parts/tower.js.
// `pragma library` = einmal geladen, kein eigener Zustand pro Import.
.pragma library

/** Rundenzeit als m:ss.mmm bzw. ss.mmm. Gibt "" statt null zurueck - QML-Text
 *  wuerde aus null sonst "null" machen. */
function time(s) {
    if (!s || s <= 0)
        return "";
    var m = Math.floor(s / 60);
    var sec = (s % 60).toFixed(3);
    if (sec.length < 6)
        sec = "0".repeat(6 - sec.length) + sec;
    return m > 0 ? m + ":" + sec : sec;
}

/** Abstand als +x.xxx. Wie fmtGap in core.js: 0 und negativ werden zu "0.000". */
function gap(s) {
    if (!s || s <= 0)
        return "0.000";
    return "+" + s.toFixed(3);
}

/** Sekundenbruchteil ohne Vorzeichen (Quali: Rueckstand auf Pole). */
function delta(s) {
    return "+" + s.toFixed(3);
}

/** Restzeit als m:ss (Gefahrenzone-Countdown).
 *  Den Quali-Untertitel im Tower-Kopf formatiert dagegen bridge.py - der wandert
 *  ohnehin schon dort durch die Session-Auswertung. */
function clock(seconds) {
    var m = Math.floor(seconds / 60);
    var s = Math.floor(seconds % 60);
    return m + ":" + (s < 10 ? "0" + s : s);
}

// ── Sektorfarben ────────────────────────────────────────────────────────────
// Die Klassennamen kommen aus derive.py (sector_view) und heissen dort genauso
// wie im CSS: sp = Session-Best (lila), sg = persoenliche Bestzeit (gruen),
// sy = langsamer (gelb).
function sectorColor(cls, invalid) {
    if (invalid)
        return "#ff3b30";        // Track-Limits: ungueltige Runde -> rot
    switch (cls) {
    case "sp": return "#b14bff";
    case "sg": return "#00d56a";
    case "sy": return "#ffd000";
    }
    return "#ffffff";
}

// ── Schadensfarbe ───────────────────────────────────────────────────────────
// tower.js dmgColor(): unter DMG_MIN gilt ein Teil als heil und bleibt dunkel,
// darueber laeuft die Farbe von Gelb ueber Orange nach Rot (25 %..85 %).
var DMG_MIN = 15;

function dmgColor(pct) {
    if (pct < DMG_MIN)
        return "#3a3a44";
    var f = Math.max(0, Math.min(1, (pct - 25) / 60));
    return Qt.rgba(1.0, (208 - 170 * f) / 255, (24 * (1 - f)) / 255, 1.0);
}

// ── Podium ──────────────────────────────────────────────────────────────────
// Die Metallverlaeufe aus tower.css (.rank-1/.rank-2/.rank-3), jeweils die
// 180deg-Ebene - die schraege Glanzebene malt MedalText.qml selbst.
function medalStops(rank) {
    switch (rank) {
    case 1: return ["#FFF3A6", "#FFD700", "#C99100", "#FFE680"];   // Gold
    case 2: return ["#FFFFFF", "#C8CFD8", "#8A9099", "#EAEEF3"];   // Silber
    case 3: return ["#F6C88A", "#D2843C", "#8C5A2B", "#EBA96A"];   // Bronze
    }
    return ["#ffffff", "#ffffff", "#ffffff", "#ffffff"];
}
