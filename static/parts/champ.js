/* WM-Stand aus championship.json + Live-Projektion "wenn heute so endet"
   — 1:1 aus templates/test.html (Z. 2926–2943).

   ⚠ Der WM-Stand ist ein MANUELLES Panel: er erscheint nur, wenn er in der Regie
   (/regie) eingeschaltet ist. Das gilt hier genauso wie im Gesamt-Overlay — die
   Seite bleibt also leer, bis du ihn dort einblendest. */

const champEl = document.getElementById("champ-panel");
let champData = null, champFetching = false, champLast = 0;

function updateChamp(drivers, session, connected) {
  if (!KERS.regie.champ) { champEl.classList.remove("show"); champData = null; champLast = 0; return; }
  const now = performance.now();
  if (!champFetching && now - champLast > 3000) {   // Backend rechnet live (base + Rennposition) -> alle 3 s neu holen
    champFetching = true; champLast = now;
    fetch("/api/championship").then(r => r.json()).then(j => { champData = j; }).catch(() => {}).finally(() => { champFetching = false; });
  }
  if (!champData || !Array.isArray(champData.standings)) return;
  champEl.classList.add("show");
  const st = champData.standings, live = st.some(s => s.live_points > 0);
  champEl.innerHTML = `<div class="champ-title">${esc(champData.title || "Championship")}${live ? " · wenn heute so endet" : ""}</div>`
    + st.slice(0, 12).map((s, i) => `<div class="champ-row" style="--t:${teamColor(s.team)}"><span class="champ-pos">${i + 1}</span>`
        + `<span class="champ-name">${esc(s.name)}</span><span class="champ-pts">${s.total_points}</span>`
        + `<span class="champ-delta">${s.live_points > 0 ? "+" + s.live_points : ""}</span></div>`).join("");
}

KERS.onData((data, drivers) => updateChamp(drivers, data.session || {}, data.connected));
