/* Gefahrenzone-Countdown (Quali) — 1:1 aus templates/test.html (Z. 2845–2860).
   Zeigt in den letzten 3 Minuten von Q1/Q2 den Fahrer auf dem letzten sicheren Platz. */

const dzEl = document.getElementById("danger-zone");

function updateDangerZone(drivers, session, connected) {
  const cut = session.type === 5 ? 16 : session.type === 6 ? 10 : 0;   // Q1 -> P16, Q2 -> P10 = letzter sicherer Platz
  const tl = session.time_left || 0;
  if (!KERS.on("danger") || !connected || !session.is_quali || !cut || tl <= 0 || tl > 180) {
    dzEl.classList.remove("show"); return;                             // nur in den letzten 3 Minuten
  }
  const bubble = drivers.find(d => d.position === cut);
  if (!bubble) { dzEl.classList.remove("show"); return; }
  dzEl.querySelector(".dz-pos").textContent = "P" + cut;
  dzEl.querySelector(".dz-name").textContent = bubble.name || "";
  const m = Math.floor(tl / 60), s = Math.floor(tl % 60);
  dzEl.querySelector(".dz-time").textContent = m + ":" + String(s).padStart(2, "0");
  dzEl.classList.add("show");
}

KERS.onData((data, drivers) => updateDangerZone(drivers, data.session || {}, data.connected));
