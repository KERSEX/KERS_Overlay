/* Fastest-Lap-Banner — 1:1 aus templates/test.html (Z. 2827–2843). */

const flbEl = document.getElementById("fl-banner");
let flbSig = null, flbTimer = null;

function updateFLBanner(session, connected) {
  if (!KERS.on("flbanner") || !connected || session.is_quali) { flbEl.classList.remove("show"); return; }
  if (!(session.fastest_lap_time > 0)) return;
  const sig = (session.fastest_lap_driver || "") + "|" + session.fastest_lap_time;
  if (sig === flbSig) return;
  const first = flbSig === null;   // beim Laden mitten in der Session nicht sofort feuern
  flbSig = sig;
  if (first) return;
  flbEl.querySelector(".flb-name").textContent = session.fastest_lap_driver || "";
  flbEl.querySelector(".flb-time").textContent = fmtTime(session.fastest_lap_time) || "";
  flbEl.classList.add("show");
  clearTimeout(flbTimer);
  flbTimer = setTimeout(() => flbEl.classList.remove("show"), (CFG.flbdur || 4.5) * 1000);
}

KERS.onData(data => updateFLBanner(data.session || {}, data.connected));
