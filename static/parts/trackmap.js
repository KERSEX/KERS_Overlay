/* Trackmap — 1:1 aus templates/test.html (Z. 3034–3288).
   Kontur kommt von /api/track (live gelernt): Punkte [x, z, Rundenanteil, Sektor].
   Sektorfarben: S1 weiss, S2 blau, S3 rot. Marshal-Zonen (gelb) legen sich als
   blinkende Segmente darueber. Groesse/Drehung/Spiegelung live aus den Settings.

   ⚠ Einziger Unterschied zum Original: applyMapCorner() wird hier NICHT gerufen.
   Das Setting `mapcorner` positioniert die Karte im Gesamt-Overlay in eine
   Bildschirmecke — auf der Einzelseite klebt sie immer oben links, die Platzierung
   machst du in OBS. (Nebenbei: `mapcorner` fehlt in test.htmls DEFAULT_CFG und ist
   dort damit ohnehin wirkungslos — im Core ist es korrekt verdrahtet.) */

const tmEl = document.getElementById("trackmap");
let trackBounds = null, trackVer = -1, trackDone = false;
let trackPtsRaw = [];                       // rohe Konturpunkte
let _tmCos = 1, _tmSin = 0, _tmFlip = true, _tmSig = "";
const SECTOR_COLORS = { 0: "rgba(255,255,255,0.92)", 1: "#2f7bff", 2: "#e10600" };

function ensureTmTransform() {
  const sig = (CFG.maprot || 0) + "|" + (CFG.mapflip ? 1 : 0);
  if (sig === _tmSig) return false;
  _tmSig = sig;
  const r = (CFG.maprot || 0) * Math.PI / 180;
  _tmCos = Math.cos(r); _tmSin = Math.sin(r); _tmFlip = !!CFG.mapflip;
  return true;   // geaendert -> Pfade neu bauen
}
// Weltkoordinaten -> 0..100 ViewBox (Seitenverhaeltnis erhalten, zentriert), dann um
// die Mitte drehen + optional spiegeln. Linie und Punkte laufen beide hier durch.
function normPoint(x, z) {
  if (!trackBounds) return null;
  const { minX, minZ, sc, offX, offZ } = trackBounds;
  let nx = offX + (x - minX) * sc;
  let nz = 100 - (offZ + (z - minZ) * sc);        // z-Achse flippen
  let dx = nx - 50, dz = nz - 50;
  let rx = dx * _tmCos - dz * _tmSin;
  let rz = dx * _tmSin + dz * _tmCos;
  if (_tmFlip) rx = -rx;
  return [rx + 50, rz + 50];
}
const pathFrom = pts => pts.map((p, i) => (i ? "L" : "M") + p[0].toFixed(1) + " " + p[1].toFixed(1)).join(" ");

// Kontur leicht glaetten: Zittern aus den ~5m-Lernpunkten mitteln. Nur x/z; frac (Index 2)
// und Sektor (Index 3) bleiben pro Punkt erhalten -> Sektorgrenzen und Flaggen-Mapping
// (laufen ueber frac/Sektor) unveraendert.
function smoothLoop(pts, passes = 2) {
  if (!pts || pts.length < 8) return pts;
  let out = pts.map(p => p.slice());
  for (let pass = 0; pass < passes; pass++) {
    const src = out.map(p => p.slice());
    const n = src.length;
    for (let i = 0; i < n; i++) {
      const a = src[(i - 1 + n) % n], c = src[(i + 1) % n];
      out[i][0] = a[0] * 0.25 + src[i][0] * 0.5 + c[0] * 0.25;
      out[i][1] = a[1] * 0.25 + src[i][1] * 0.5 + c[1] * 0.25;
    }
  }
  return out;
}

// Kontur zeichnen: nach Sektoren eingefaerbt (wenn Sektor-Info da + mapflags an),
// sonst eine durchgehende weisse Linie.
function buildTrackPaths() {
  if (!trackPtsRaw.length || !trackBounds) return;
  const g = document.getElementById("tm-track");
  const hasMeta = trackPtsRaw[0].length >= 4 && CFG.mapflags;
  // Unterste Schicht: EINE durchgehende dunkle Casing-Linie fuer die ganze Kontur.
  const casing = `<path class="tm-casing" d="${pathFrom(trackPtsRaw.map(p => normPoint(p[0], p[1])))} Z"></path>`;
  let colors = "";
  if (!hasMeta) {
    colors = `<path class="tm-line" d="${pathFrom(trackPtsRaw.map(p => normPoint(p[0], p[1])))} Z"></path>`;
  } else {
    let run = [trackPtsRaw[0]];
    const flush = (bridge) => {
      const seg = bridge ? run.concat([bridge]) : run;
      if (seg.length > 1) {
        const sec = run[0][3] || 0;
        colors += `<path class="tm-line" style="stroke:${SECTOR_COLORS[sec] || SECTOR_COLORS[0]}" d="${pathFrom(seg.map(p => normPoint(p[0], p[1])))}"></path>`;
      }
    };
    for (let i = 1; i < trackPtsRaw.length; i++) {
      const p = trackPtsRaw[i];
      if ((p[3] || 0) === (run[0][3] || 0)) run.push(p);
      else { flush(p); run = [p]; }
    }
    flush(trackPtsRaw[0]);   // Schleife schliessen
  }
  g.innerHTML = casing + colors;
}

// Marshal-Zonen: gelb geflaggte Abschnitte blinken ueber der Kontur.
let _flagSig = "";
function buildFlagPaths(zones) {
  const g = document.getElementById("tm-flags");
  const usable = CFG.mapflags && zones && zones.length && trackPtsRaw.length
    && trackPtsRaw[0].length >= 3 && trackPtsRaw[0][2] >= 0 && trackBounds;
  if (!usable) { if (g.innerHTML) g.innerHTML = ""; _flagSig = ""; return; }
  const sig = zones.map(z => z.f).join(",") + "|" + _tmSig + "|" + trackVer;
  if (sig === _flagSig) return;
  _flagSig = sig;
  const inZone = (frac, a, b) => a <= b ? (frac >= a && frac < b) : (frac >= a || frac < b);   // mit Wrap
  let html = "";
  zones.forEach((z, i) => {
    if (z.f !== 3) return;   // nur gelbe Flaggen
    const b = i + 1 < zones.length ? zones[i + 1].s : zones[0].s;
    let run = [];
    const flush = () => { if (run.length > 1) html += `<path class="tm-flag" d="${pathFrom(run.map(p => normPoint(p[0], p[1])))}"></path>`; run = []; };
    // Nur Punkte mit gueltigem Rundenanteil (0..1). frac === -1 (gelernt bevor track_length
    // bekannt war) darf NICHT flaggen: bei einer ueber Start/Ziel wrappenden Zone (a > b) waere
    // sonst `-1 < b` fuer JEDEN Punkt wahr -> die ganze Kontur wuerde gelb ("Full Course"-Bug).
    trackPtsRaw.forEach(p => { if (p[2] >= 0 && p[2] <= 1 && inZone(p[2], z.s, b)) run.push(p); else flush(); });
    flush();
  });
  g.innerHTML = html;
}

async function refreshTrack() {
  try {
    const r = await fetch("/api/track");
    const t = await r.json();
    trackDone = !!t.done;
    // Karte wird erst gezeichnet UND gezeigt, wenn die Strecke fertig gelernt ist.
    if (!t.done || !t.pts || t.pts.length < 20) return;
    if (t.ver !== trackVer) {
      trackVer = t.ver;
      trackPtsRaw = smoothLoop(t.pts);   // Kontur glaetten (Zittern raus), frac/Sektor bleiben
      let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
      trackPtsRaw.forEach(([x, z]) => { minX = Math.min(minX, x); maxX = Math.max(maxX, x); minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z); });
      const spanX = maxX - minX || 1, spanZ = maxZ - minZ || 1;
      const pad = 15, size = 100 - 2 * pad;   // Rand -> beim Drehen wird nichts abgeschnitten
      const sc = size / Math.max(spanX, spanZ);          // EIN Faktor -> keine Verzerrung
      trackBounds = { minX, minZ, sc,
                      offX: pad + (size - spanX * sc) / 2,
                      offZ: pad + (size - spanZ * sc) / 2 };
      ensureTmTransform();
      buildTrackPaths();
      _flagSig = "";
    }
  } catch (e) { /* Server weg -> Karte bleibt wie sie ist */ }
}

const carMove = {};   // idx -> {x,z,t}: letzte deutliche Bewegung (fuer "steht"-Erkennung)
const dnfGhost = {};  // idx -> {t,x,z}: on-track Ausgeschiedener -> Punkt bleibt kurz stehen
const dnfDone = new Set();   // idx: Ghost schon einmal durchgelaufen -> nicht neu anlegen
const lastKnownPos = {};   // idx -> [x,z]: letzte bekannte Weltposition (ueberlebt DNF)
const DNF_LINGER = 12000;   // ms, wie lange der Ghost-Punkt sichtbar bleibt
let _lastFlagsCfg = null;

function updateTrackmap(drivers, focusIdx, connected, session) {
  // Nur zeigen, wenn verbunden UND die Strecke fertig gebaut ist.
  if (!KERS.on("map") || !connected || !trackBounds || !trackDone) { tmEl.classList.remove("show"); return; }
  tmEl.classList.add("show");
  // Groesse + Ausrichtung live aus den Settings (mapcorner s. Kopfkommentar)
  const px = (CFG.mapsize || 400) + "px";
  if (tmEl.style.width !== px) { tmEl.style.width = px; tmEl.style.height = px; }
  if (ensureTmTransform() || _lastFlagsCfg !== CFG.mapflags) { _lastFlagsCfg = CFG.mapflags; buildTrackPaths(); _flagSig = ""; }
  buildFlagPaths(session && session.marshal_zones);
  const g = document.getElementById("tm-cars");
  const seen = new Set();
  const now = performance.now();
  const ds = CFG.dotsize || 1;
  // DNF/DSQ merken: on track (nicht in der Box) aufgegeben -> Position einfrieren, Punkt
  // bleibt danach kurz verblassend stehen. In der Box aufgegeben -> gar kein Track-Punkt.
  drivers.forEach(d => {
    if (d.pos_xz) lastKnownPos[d.index] = d.pos_xz;   // letzte bekannte Weltposition
    const retired = d.dnf || d.dsq;
    const lp = lastKnownPos[d.index];
    if (retired && d.in_pit) delete dnfGhost[d.index];
    else if (retired && lp) { if (!dnfGhost[d.index] && !dnfDone.has(String(d.index))) dnfGhost[d.index] = { t: now, x: lp[0], z: lp[1], col: resolveColor(teamColor(d.team)) }; }
    else if (!retired) { delete dnfGhost[d.index]; dnfDone.delete(String(d.index)); }   // wieder aktiv -> Merker zurueck
  });
  drivers.forEach(d => {
    if (!d.pos_xz || d.dnf || d.dsq || d.in_pit) return;   // ausgeschieden / Box -> normaler Dot aus
    if (session && session.is_quali && (d.driver_status || 0) === 0) return;   // Garage -> nicht auf die Map
    // Stehende Autos ausblenden: seit >1,5 s < 3 m bewegt -> steht (geparkt/Crash) -> weg
    const wx = d.pos_xz[0], wz = d.pos_xz[1];
    const rec = carMove[d.index];
    if (!rec) { carMove[d.index] = { x: wx, z: wz, t: now }; }
    else if (Math.hypot(wx - rec.x, wz - rec.z) > 3) { rec.x = wx; rec.z = wz; rec.t = now; }
    else if (now - rec.t > 1500) return;                   // steht -> Dot nicht zeichnen
    const p = normPoint(wx, wz);
    if (!p) return;
    seen.add(String(d.index));
    let grp = g.querySelector(`[data-idx="${d.index}"]`);
    if (!grp) {
      grp = document.createElementNS("http://www.w3.org/2000/svg", "g");
      grp.setAttribute("data-idx", d.index);
      const c = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      c.setAttribute("class", "tm-dot");
      c.setAttribute("fill", resolveColor(teamColor(d.team)));   // Farbe aendert sich nie -> nur einmal
      const tx = document.createElementNS("http://www.w3.org/2000/svg", "text");
      tx.setAttribute("class", "tm-num");
      grp.appendChild(c); grp.appendChild(tx);
      g.appendChild(grp);
    }
    const c = grp.firstChild, tx = grp.lastChild;
    const r = (d.index === focusIdx ? 3.4 : 2.9) * ds;
    c.setAttribute("cx", p[0].toFixed(1));
    c.setAttribute("cy", p[1].toFixed(1));
    c.setAttribute("r", r.toFixed(2));
    c.classList.toggle("focused", d.index === focusIdx);
    c.classList.remove("retired"); grp.style.opacity = "";   // (falls vorher Ghost) wieder normal
    if (CFG.mapnumbers) {
      tx.style.display = "";
      tx.setAttribute("x", p[0].toFixed(1));
      tx.setAttribute("y", (p[1] + r * 0.42).toFixed(1));
      tx.setAttribute("font-size", (r * 1.15).toFixed(1));
      const pos = String(d.position);
      if (tx.textContent !== pos) tx.textContent = pos;
    } else tx.style.display = "none";
  });
  // Ghost-Punkte: on-track Ausgeschiedene bleiben an ihrer Stelle stehen und verblassen.
  for (const idx in dnfGhost) {
    const gh = dnfGhost[idx];
    const age = now - gh.t;
    const p = age < DNF_LINGER ? normPoint(gh.x, gh.z) : null;
    if (!p) { delete dnfGhost[idx]; dnfDone.add(String(idx)); continue; }   // abgelaufen -> merken
    seen.add(String(idx));
    let grp = g.querySelector(`[data-idx="${idx}"]`);
    if (!grp) {
      grp = document.createElementNS("http://www.w3.org/2000/svg", "g");
      grp.setAttribute("data-idx", idx);
      grp.appendChild(document.createElementNS("http://www.w3.org/2000/svg", "circle"));
      const tx = document.createElementNS("http://www.w3.org/2000/svg", "text");
      tx.setAttribute("class", "tm-num"); grp.appendChild(tx);
      g.appendChild(grp);
    }
    const c = grp.firstChild;
    c.setAttribute("class", "tm-dot retired");
    c.setAttribute("fill", gh.col || "#767680");   // Punkt bleibt in Teamfarbe
    c.setAttribute("cx", p[0].toFixed(1));
    c.setAttribute("cy", p[1].toFixed(1));
    c.setAttribute("r", (2.9 * ds).toFixed(2));
    grp.lastChild.style.display = "none";                         // keine Positionsnummer beim Ausgeschiedenen
    grp.style.opacity = (1 - age / DNF_LINGER).toFixed(2);        // verblassen
  }
  // Verschwundene Autos entfernen
  g.querySelectorAll("g[data-idx]").forEach(el => { if (!seen.has(el.dataset.idx)) el.remove(); });
}

// CSS-Variable (var(--team-x)) -> echter Farbwert fuer SVG fill
const _colorCache = {};
function resolveColor(v) {
  if (!v || v[0] === "#") return v || "#fff";
  if (_colorCache[v]) return _colorCache[v];
  const name = v.replace(/var\(|\)/g, "").trim();
  const c = getComputedStyle(document.documentElement).getPropertyValue(name).trim() || "#fff";
  _colorCache[v] = c; return c;
}

KERS.onData((data, drivers) => {
  updateTrackmap(drivers, data.focus_index, data.connected, data.session || {});
});

// Kontur aktuell halten (bis gelernt) — wie in test.html Z. 3402–3403
refreshTrack();
setInterval(refreshTrack, 2000);
