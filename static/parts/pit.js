/* Live-Pit-Timer — 1:1 aus templates/test.html (Z. 1650–1708).

   ⚠ Die Boxen-Erkennung (wer faehrt gerade rein/raus?) steckt im Gesamt-Overlay
   MITTEN in der Tower-Fahrerschleife (test.html Z. 2148–2168) — startPitStop wird
   dort bei Z. 2162 gerufen. Ohne Tower gibt es diese Schleife nicht, deshalb laeuft
   sie hier als eigene Schleife unten. Die Logik ist unveraendert uebernommen. */

const pitTrack = {};   // Boxen-Zustand je Fahrer
const PIT_LOSS = 22;   // grobe Sekunden-Schaetzung fuer den Zeitverlust eines Stopps

// Live-Pit-Timer: bei der Einfahrt eine Card mit hochzaehlender Zeit anlegen (Zeitstempel im
// dataset, tickPitCards() zaehlt fluessig hoch). Alter Reifen wird gemerkt.
function startPitStop(d, drivers) {
  const stack = document.getElementById("pit-stack");
  const card = document.createElement("div");
  card.className = "pit-card live";
  card.style.setProperty("--pt", TEAM_COLORS[d.team] || "#fff");
  card.dataset.t0 = String(performance.now());
  // #1: ungefaehre Auskommen-Position nach dem Stopp (gap + Pit-Verlust -> ins Feld einsortieren)
  let exitHtml = "";
  if (drivers) {
    const projGap = (d.gap_to_leader || 0) + PIT_LOSS;
    let projPos = 1;
    drivers.forEach(o => { if (o.index !== d.index && !o.dnf && !o.dsq && (o.gap_to_leader || 0) < projGap) projPos++; });
    exitHtml = `<div class="pp-exit">Box out window <b>P${projPos}</b></div>`;
  }
  card.innerHTML = `<div class="pp-head">IN DER BOX</div>`
    + `<div class="pp-body"><span class="pp-name">${esc(d.name)}</span>`
    + `<span class="pp-tyres">${pitTyreImg(d.compound)}</span>`
    + `<span class="pp-time">0.0s</span></div>` + exitHtml;
  stack.appendChild(card);                                   // neuer Stopp unten an den Stack
  while (stack.children.length > 3) stack.removeChild(stack.firstChild);   // max. 3, aeltester raus
  return card;
}
// Frontfluegel gilt als gewechselt, wenn er bei der Einfahrt beschaedigt war und bei der
// Ausfahrt (fast) heil ist -> das Spiel setzt den Schaden bei neuem Fluegel auf ~0.
const FW_CHANGE_MIN = 10;
// Ausfahrt: Live-Timer stoppen, finale Standzeit (d.pit_time) einblenden, Reifenwechsel
// zeigen (falls bekannt/geaendert), neuen Frontfluegel markieren und die Card nach 5 s ausblenden.
function finishPitStop(card, d, oldC, newC, entryFw) {
  if (!card) return;
  card.classList.remove("live");
  card.querySelector(".pp-head").textContent = "STANDZEIT";
  const tyres = card.querySelector(".pp-tyres");
  if (TYRE_ICONS[oldC] && TYRE_ICONS[newC] && oldC !== newC)
    tyres.innerHTML = `${pitTyreImg(oldC)}<span class="pp-arrow">→</span>${pitTyreImg(newC, "pp-spin")}`;
  else
    tyres.innerHTML = pitTyreImg(newC || oldC);
  // Frontfluegel-Wechsel: war beschaedigt bei Einfahrt, bei Ausfahrt auf ~0 -> neuer Fluegel.
  const curFw = Math.max(d.dmg_fl || 0, d.dmg_fr || 0);
  if ((entryFw || 0) >= FW_CHANGE_MIN && curFw <= entryFw * 0.5) {
    const fw = document.createElement("span");
    fw.className = "pp-fw"; fw.title = "Neuer Frontflügel";
    tyres.insertAdjacentElement("afterend", fw);            // zwischen Reifen und Zeit
  }
  card.querySelector(".pp-time").textContent = d.pit_time > 0 ? d.pit_time.toFixed(1) + "s" : "—";
  setTimeout(() => { card.classList.add("leaving"); setTimeout(() => card.remove(), 400); }, 5000);
}
// Grosse Zeit der laufenden Pit-Cards fluessig hochzaehlen.
function tickPitCards() {
  document.querySelectorAll("#pit-stack .pit-card.live").forEach(card => {
    const t0 = parseFloat(card.dataset.t0 || "0");
    const el = card.querySelector(".pp-time");
    if (el) el.textContent = ((performance.now() - t0) / 1000).toFixed(1) + "s";
  });
  requestAnimationFrame(tickPitCards);
}
requestAnimationFrame(tickPitCards);

/* Boxen-Ein-/Ausfahrt erkennen — Logik aus test.html Z. 2148–2168, dort Teil der
   Tower-Fahrerschleife. */
KERS.onData((data, drivers) => {
  const sessionData = data.session || {};
  if (!data.connected) return;
  drivers.forEach(d => {
    const pt = pitTrack[d.index] || (pitTrack[d.index] = { in: false, comp: d.compound, lastComp: d.compound, card: null });
    // Compound AUSSERHALB der Box laufend merken -> das ist der wahre "alte" Reifen. Wichtig,
    // weil manche Quellen/die Test-GUI den NEUEN Compound schon im Moment der Einfahrt melden.
    if (!d.in_pit) pt.lastComp = d.compound;
    // Live-Pit-Timer (nur Rennen, ab Runde 2 -> der Startaufstellungs-Compound zaehlt nicht,
    // und nicht fuer DNF/DSQ-Fahrer).
    if (d.in_pit && !pt.in) {
      pt.in = true; pt.comp = pt.lastComp; pt.fw = Math.max(d.dmg_fl || 0, d.dmg_fr || 0);
      pt.card = (!sessionData.is_quali && (sessionData.current_lap || 0) >= 2 && !d.dnf && !d.dsq)
                ? startPitStop(d, drivers) : null;
    } else if (!d.in_pit && pt.in) {
      pt.in = false;
      if (pt.card) { finishPitStop(pt.card, d, pt.comp, d.compound, pt.fw || 0); pt.card = null; }
    }
    // DNF/DSQ waehrend des Stopps -> laufende Card sofort entfernen.
    if ((d.dnf || d.dsq) && pt.card) { pt.card.remove(); pt.card = null; }
  });
});
