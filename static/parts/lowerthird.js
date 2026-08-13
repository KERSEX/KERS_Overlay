/* Lower-Third — 1:1 aus templates/test.html (Z. 2963–2998).
   Erscheint beim Kamerawechsel im Rennen und blendet nach CFG.ltdur wieder aus.

   ⚠ stintLen (Stint-Chips) kommt aus core.js (deriveShared -> trackStints) —
   im Gesamt-Overlay wird trackStints aus render() gerufen. */

const ltEl = document.getElementById("lower-third");
let ltFocus = null, ltTimer = null;

// Reifenfarben fuer Compounds ohne eigenes Icon
const ST_TYRE_HEX = { S: "#E8002D", M: "#FFC906", H: "#EBEBEB", I: "#39B54A", W: "#0067FF" };

function updateLowerThird(drivers, focusIdx, session, connected) {
  // Nur im Rennen (in der Quali kein Lower-Third)
  if (!KERS.on("lowerthird") || !connected || session.is_quali || (session.current_lap || 0) < 3) { ltEl.classList.remove("show"); ltFocus = focusIdx; return; }
  if (focusIdx === ltFocus) return;
  ltFocus = focusIdx;
  const d = drivers.find(x => x.index === focusIdx);
  if (!d) return;
  ltEl.style.setProperty("--lt-team", teamColor(d.team));
  ltEl.querySelector(".lt-pos").textContent = "P" + d.position;
  ltEl.querySelector(".lt-name").textContent = d.name;
  setTyreIcon(ltEl.querySelector(".lt-sub img"), d.compound);
  ltEl.querySelector(".lt-sub .lt-team").textContent = d.team || "";
  // #1: Reifenalter + Reifenstrategie (Stints) direkt im Lower-Third
  ltEl.querySelector(".lt-sub .lt-age").textContent = (d.compound && d.tyre_age >= 0) ? "· Rnd " + d.tyre_age : "";
  const stratEl = ltEl.querySelector(".lt-strat");
  // Stints als Reifen-Bild + dahinter die gefahrenen Runden (aktueller Reifen = tyre_age)
  const stints = [...(stintLen[d.index] || []), { c: d.compound, laps: d.tyre_age || 0 }].filter(s => s.c);
  if (CFG.strat && stints.length && d.compound) {
    stratEl.innerHTML = stints.map(s => TYRE_ICONS[s.c]
      ? `<span class="lt-stint"><img class="lt-tyre" src="static/tyres/${s.c}.png" alt="${s.c}"><span class="lt-laps">${s.laps}</span></span>`
      : `<span class="lt-stint"><span class="lt-seg" style="background:${ST_TYRE_HEX[s.c] || "#888"}">${s.c}</span><span class="lt-laps">${s.laps}</span></span>`
    ).join("");
    stratEl.style.display = "";
  } else stratEl.style.display = "none";
  // Interval (Abstand zum Vordermann), nicht Gap zum Fuehrenden
  ltEl.querySelector(".lt-gap").textContent =
    d.position === 1 ? "LEADER"
    : d.laps_down >= 1 ? "+" + d.laps_down + " LAP" + (d.laps_down > 1 ? "S" : "")
    : fmtGap(d.gap_to_ahead);
  ltEl.classList.add("show");
  clearTimeout(ltTimer);
  ltTimer = setTimeout(() => ltEl.classList.remove("show"), (CFG.ltdur || 4) * 1000);
}

KERS.onData((data, drivers) => updateLowerThird(drivers, data.focus_index, data.session || {}, data.connected));
