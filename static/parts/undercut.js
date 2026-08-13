/* Undercut-Alarm — 1:1 aus templates/test.html (Z. 3000–3032).
   Fahrer B boxt < 2,5 s hinter A -> "Undercut-Versuch". Sobald A auch geboxt hat
   und wieder draussen ist: Positionen vergleichen -> funktioniert / gescheitert.

   ⚠ Der Undercut-Alarm hat KEINE eigene Anzeige — seine Ausgabe sind Meldungen im
   selben Banner wie die Rennleitung. Diese Seite zeigt also nur Undercut-Meldungen;
   /part/racemsg zeigt Strafen/Track-Limits/Flaggen/Safety Car. Wer beides an einer
   Stelle will, nimmt nur /part/racemsg und laesst diese Seite weg — dann fehlen
   allerdings die Undercut-Meldungen. Beide zusammen = zwei getrennt platzierbare
   Banner. */

const ucPitState = {};
let ucAttempts = [];

function updateUndercut(drivers, session, connected) {
  if (!KERS.on("undercut") || session.is_quali || !connected) { ucAttempts = []; return; }
  const byIdx = {};
  drivers.forEach(d => byIdx[d.index] = d);
  drivers.forEach(d => {
    const was = ucPitState[d.index] || false;
    if (d.in_pit && !was) {   // Boxen-Einfahrt
      const ahead = drivers.find(x => x.position === d.position - 1);
      if (ahead && !ahead.in_pit && d.gap_to_ahead > 0 && d.gap_to_ahead < 2.5) {
        ucAttempts.push({ a: ahead.index, b: d.index, an: ahead.name, bn: d.name, phase: 1, lap0: session.current_lap || 0 });
        raceMsg("⛏", "UNDERCUT-VERSUCH: " + esc(d.name) + " auf " + esc(ahead.name), "#ffcc00");
      }
    }
    ucPitState[d.index] = d.in_pit;
  });
  ucAttempts = ucAttempts.filter(u => {
    const A = byIdx[u.a], B = byIdx[u.b];
    if (!A || !B || A.dnf || B.dnf || A.dsq || B.dsq) return false;
    if ((session.current_lap || 0) - u.lap0 > 6) return false;   // A boxt nicht -> verjaehrt
    if (u.phase === 1 && A.in_pit) u.phase = 2;                  // Gegner ist in der Box
    if (u.phase === 2 && !A.in_pit) {                            // Gegner wieder draussen -> Ergebnis
      if (B.position < A.position) raceMsg("✓", "UNDERCUT FUNKTIONIERT: " + esc(u.bn) + " vor " + esc(u.an), "#00e676");
      else raceMsg("✗", "UNDERCUT GESCHEITERT: " + esc(u.an) + " bleibt vor " + esc(u.bn), "#ff5a5a");
      return false;
    }
    return true;
  });
}

KERS.onData((data, drivers) => updateUndercut(drivers, data.session || {}, data.connected));
