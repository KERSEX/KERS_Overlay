/* Pit-Fenster-Projektion — 1:1 aus templates/test.html (Z. 2945–2961).
   Zeigt nur, wenn der Kamera-Fahrer Schaden >= 30 % hat (muss also zum Fluegelwechsel):
   wo kaeme er nach einem Stopp wieder raus? */

const ppEl = document.getElementById("pit-proj");
const PIT_LOSS = 22;   // grobe Sekunden-Schaetzung fuer den Zeitverlust eines Stopps

function updatePitProj(drivers, focusIdx, session, connected) {
  if (!KERS.on("pitproj") || !connected || session.is_quali) { ppEl.classList.remove("show"); return; }
  const d = drivers.find(x => x.index === focusIdx);
  // #1: nur zeigen, wenn der Kamera-Fahrer SCHADEN hat (muss zum Fluegelwechsel) -> wo kaeme er raus?
  const dmg = d ? Math.max(d.dmg_fl || 0, d.dmg_fr || 0, d.dmg_rw || 0) : 0;
  if (!d || d.dnf || d.dsq || d.in_pit || dmg < 30) { ppEl.classList.remove("show"); return; }
  const projGap = (d.gap_to_leader || 0) + PIT_LOSS;
  let projPos = 1;
  drivers.forEach(o => { if (o.index !== d.index && !o.dnf && !o.dsq && (o.gap_to_leader || 0) < projGap) projPos++; });
  ppEl.classList.add("show");
  ppEl.innerHTML = `<div class="pp-hd">Nach Boxenstopp · ${esc(d.name)}</div>`
    + `<div class="pp-flow"><div class="pp-box cur"><div class="num">P${d.position}</div><div class="lbl">Jetzt</div></div>`
    + `<div class="pp-arr">→</div><div class="pp-box prj"><div class="num">P${projPos}</div><div class="lbl">danach</div></div></div>`;
}

KERS.onData((data, drivers) => updatePitProj(drivers, data.focus_index, data.session || {}, data.connected));
