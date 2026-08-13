/* Verlaufs-Charts (Gap / Positionen / Rundenzeiten) — 1:1 aus templates/test.html
   (Z. 3280–3369).

   Gesteuert wird das Chart ueber die Regie (/regie) bzw. die Tasten P/G/L im
   Gesamt-Overlay — beides schickt denselben Befehl an den Server, der im Payload
   als `regie.chart` zurueckkommt. Diese Seite liest ihn aus KERS.regie.chart.
   In OBS bekommt eine Browser-Quelle keine Tastendruecke, dort ist die Regie
   der Weg.

   gapHist/lapHist/lastDrivers werden in core.js (deriveShared) gefuellt. */

const pcEl = document.getElementById("poschart");
let pcOpen = false, pcMode = "pos";

// CSS-Variable (var(--team-x)) -> echter Farbwert fuer SVG stroke/fill
const _colorCache = {};
function resolveColor(v) {
  if (!v || v[0] === "#") return v || "#fff";
  if (_colorCache[v]) return _colorCache[v];
  const name = v.replace(/var\(|\)/g, "").trim();
  const c = getComputedStyle(document.documentElement).getPropertyValue(name).trim() || "#fff";
  _colorCache[v] = c; return c;
}

async function drawPosChart() {
  const svg = document.getElementById("pc-svg");
  const title = pcEl.querySelector(".pc-title");
  if (title) title.textContent = pcMode === "gap" ? "Gap-Verlauf (zum Führenden)"
    : pcMode === "lap" ? "Rundenzeiten-Rückstand (zur schnellsten Runde)" : "Positionsverlauf";
  const rect = svg.getBoundingClientRect();
  const W = rect.width || 1100, H = rect.height || 560;
  svg.setAttribute("viewBox", `0 0 ${W} ${H}`);

  // Datenquelle je Modus: Positionen vom Server (Packet 15), Gaps aus der Live-Historie
  let lapsObj, meta;
  if (pcMode === "gap") {
    lapsObj = gapHist;
    meta = {}; lastDrivers.forEach(d => meta[d.index] = { name: d.name, team: d.team });
  } else if (pcMode === "lap") {
    // Rundenzeiten als Rueckstand zur schnellsten Runde im Datensatz (0 = schnellste)
    let minL = Infinity;
    Object.values(lapHist).forEach(o => Object.values(o).forEach(v => { if (v > 0 && v < minL) minL = v; }));
    lapsObj = {};
    Object.keys(lapHist).forEach(L => { lapsObj[L] = {};
      Object.keys(lapHist[L]).forEach(k => { const v = lapHist[L][k]; if (v > 0) lapsObj[L][k] = Math.round((v - minL) * 100) / 100; }); });
    meta = {}; lastDrivers.forEach(d => meta[d.index] = { name: d.name, team: d.team });
  } else {
    let data;
    try { data = await (await fetch("/api/lap_positions")).json(); }
    catch (e) { svg.innerHTML = `<text x="20" y="30" class="pc-axis">Keine Daten</text>`; return; }
    lapsObj = data.laps; meta = data.drivers || {};
  }
  const laps = Object.keys(lapsObj).map(Number).sort((a, b) => a - b);
  if (!laps.length) { svg.innerHTML = `<text x="20" y="30" class="pc-axis">Noch keine Rundendaten</text>`; return; }
  const maxLap = laps[laps.length - 1];
  const zeroBased = pcMode === "gap" || pcMode === "lap";        // 0-basierte Delta-Skala vs. Positionen
  // Y-Skala: Positionen 1..maxPos ODER Delta 0..maxDelta
  let maxY = 0;
  laps.forEach(L => Object.values(lapsObj[L]).forEach(v => { if (v > maxY) maxY = v; }));
  if (zeroBased) maxY = Math.max(1, Math.ceil(maxY / 5) * 5);
  const padL = 48, padR = 150, padT = 20, padB = 34;
  const x = L => padL + (maxLap <= 1 ? 0 : (L - 1) / (maxLap - 1)) * (W - padL - padR);
  const y = v => zeroBased
    ? padT + (v / maxY) * (H - padT - padB)                       // Delta: 0 oben (Bester)
    : padT + (maxY <= 1 ? 0 : (v - 1) / (maxY - 1)) * (H - padT - padB);
  let out = "";
  // Gitter + Y-Achse
  const ySteps = zeroBased ? 5 : maxY;
  for (let s = 0; s <= ySteps; s++) {
    const v = zeroBased ? (maxY / ySteps) * s : s + 1;
    if (!zeroBased && v > maxY) break;
    out += `<line class="pc-grid" x1="${padL}" y1="${y(v).toFixed(1)}" x2="${W-padR}" y2="${y(v).toFixed(1)}"></line>`;
    out += `<text class="pc-axis" x="${padL-8}" y="${(y(v)+4).toFixed(1)}" text-anchor="end">${zeroBased ? "+" + v.toFixed(0) + "s" : "P" + v}</text>`;
  }
  const lapStep = Math.max(1, Math.round(maxLap / 12));
  for (let L = 1; L <= maxLap; L += lapStep)
    out += `<text class="pc-axis" x="${x(L).toFixed(1)}" y="${H-12}" text-anchor="middle">${L}</text>`;
  // Linien je Fahrer
  const carIdxs = new Set();
  laps.forEach(L => Object.keys(lapsObj[L]).forEach(k => carIdxs.add(k)));
  carIdxs.forEach(idx => {
    const m = meta[idx] || { name: "#" + idx, team: "" };
    const col = resolveColor(teamColor(m.team));
    let pts = [], lastV = null;
    laps.forEach(L => { const v = lapsObj[L][idx]; if (v !== undefined && v !== null) { pts.push([x(L), y(v)]); lastV = v; } });
    if (pts.length < 1) return;
    out += `<path class="pc-track" style="stroke:${col}" d="${pts.map((pt, i) => (i ? "L" : "M") + pt[0].toFixed(1) + " " + pt[1].toFixed(1)).join(" ")}"></path>`;
    if (lastV !== null) {
      const last = pts[pts.length - 1];
      out += `<circle cx="${last[0].toFixed(1)}" cy="${last[1].toFixed(1)}" r="3.5" fill="${col}"></circle>`;
      out += `<text class="pc-endlbl" style="fill:${col}" x="${(last[0]+8).toFixed(1)}" y="${(last[1]+4).toFixed(1)}">${esc(m.name)}</text>`;
    }
  });
  svg.innerHTML = out;
}

/* Regie-Schalter auswerten — im Gesamt-Overlay macht das applyRegie() direkt,
   hier haengt die Seite sich in den Datenstrom (core.js pflegt KERS.regie). */
KERS.onData(() => {
  const want = KERS.regie.chart;   // "pos" | "gap" | "lap" | null
  if (want) {
    if (!pcOpen || pcMode !== want) { pcMode = want; pcOpen = true; pcEl.classList.add("show"); drawPosChart(); }
  } else if (pcOpen) { pcOpen = false; pcEl.classList.remove("show"); }
});

setInterval(() => { if (pcOpen) drawPosChart(); }, 3000);   // Chart offen -> nachziehen
