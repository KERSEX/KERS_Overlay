// Gemeinsame Helfer der F1-TV-Szene (Design "F1 TV", ab 0.3.1).
//
//     import "F1.js" as F1
//     F1.kuerzel("Max Verstappen")   -> "VER"
//
// Vorher stand jede Funktion in jeder Datei einzeln.
.pragma library
.import "../Fmt.js" as Fmt

/** Drei Buchstaben aus dem Namen - das Spiel liefert keine Kuerzel.
 *  "Max Verstappen" -> VER, "PEDROESCROD98" -> PED, "MERCEDES #44" -> MER */
function kuerzel(n) {
    var w = String(n || "").toUpperCase().split(/\s+/)
        .map(function (s) { return s.replace(/[^A-ZÄÖÜ]/g, ""); })
        .filter(function (s) { return s.length > 0; });
    if (w.length === 0)
        return String(n || "").toUpperCase().slice(0, 3);
    var t = w[w.length - 1];
    if (t.length < 3 && w.length > 1)
        t = w[0];
    return t.slice(0, 3);
}

/** Nachname, wenn der Name aus mehreren Woertern besteht, sonst alles. */
function nachname(n) {
    var w = String(n || "").trim().split(/\s+/);
    var letzt = w[w.length - 1];
    return (w.length > 1 && /^[^0-9#]+$/.test(letzt)) ? letzt : String(n || "");
}

/** Farbe des Reifenbuchstabens wie im Fernsehen; unbekannt -> `sonst`. */
function reifenFarbe(c, sonst) {
    switch (c) {
    case "S": return "#ff3333";
    case "M": return "#ffd400";
    case "H": return "#f2f2f2";
    case "I": return "#2bd04b";
    case "W": return "#2e8bff";
    }
    return sonst;
}

/** Laufende Zeit mit Zehnteln wie im Fernsehen: 91.37 -> "1:31.3"
 *  (abgeschnitten, nicht gerundet - die Uhr soll nicht vorlaufen). */
function zehntel(s) {
    var t = Fmt.time(Math.floor(s * 10) / 10);
    return t.length > 2 ? t.slice(0, -2) : t;
}

/** Delta aufs Ziel: -0.315 / +0.110 */
function delta(d) {
    return (d <= 0 ? "-" : "+") + Math.abs(d).toFixed(3);
}
