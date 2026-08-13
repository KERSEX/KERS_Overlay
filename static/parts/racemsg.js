/* Rennleitungs-Meldungen — Auslöser 1:1 aus templates/test.html
   (Z. 2258–2275 = race_control, Z. 1906–1919 = Safety-Car-Wechsel).
   Der Banner selbst steckt in _msgbanner.js.

   ⚠ Der rote Glow-Rahmen nach einer roten Flagge (`rf-active`) gehoert zum Tower
   und bleibt dort — hier kommt nur die Meldung. */

let lastRcId = 0;

KERS.onData(data => {
  // Rennleitung: nur das Wichtige — Track Limits (gelb) und Strafen (rot).
  // (Ueberholungen werden bewusst NICHT mehr gemeldet.)
  const rc = data.race_control || [];
  if (rc.length && rc[rc.length - 1].id < lastRcId) lastRcId = 0;   // neue Session -> IDs neu
  rc.forEach(mEntry => {
    if (mEntry.id <= lastRcId) return;
    lastRcId = mEntry.id;
    if (mEntry.type === "penalty") raceMsg("⚑", esc(mEntry.text), "#ff5a5a");
    else if (mEntry.type === "tracklimit") raceMsg("⚠", esc(mEntry.text), "#ffcc00");
    else if (mEntry.type === "mom") raceMsg("⚡", esc(mEntry.text), "#2fd9ff");
    else if (mEntry.type === "fl") { /* grosses FL-Banner uebernimmt (flbanner.html) */ }
    else if (mEntry.type === "retire") raceMsg("✖", esc(mEntry.text), "#b0b6c0");
    else if (mEntry.type === "redflag") raceMsg("⚑", esc(mEntry.text), "#ff2a1a");
    else if (mEntry.type === "winner") raceMsg("🏆", esc(mEntry.text), "#ffd700");
    else if (mEntry.type === "flag") raceMsg("🏁", esc(mEntry.text), "#ffffff");
    else if (mEntry.type === "penserved") raceMsg("✓", esc(mEntry.text), "#00e676");
    else raceMsg("•", esc(mEntry.text), "#9aa0aa");
  });
});

// Safety-Car-Statuswechsel. Das Umschalten von scOnboardActive/scRestartUntilLap
// passiert in core.js (deriveShared) — hier nur die sichtbare Meldung.
KERS.onSc((status, prev) => {
  if (status === "sc" || status === "vsc") {
    raceMsg("⚠", status === "sc" ? "SAFETY CAR" : "VIRTUAL SAFETY CAR", "#ffcc00");
  } else if (status === "none" && (prev === "sc" || prev === "vsc")) {
    raceMsg("✓", "RENNEN FREIGEGEBEN", "#00e676");
  }
});
