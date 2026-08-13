/* Battle-Boxen — 1:1 aus templates/test.html (Z. 1733–1751, 2328–2543).
   Braucht aus core.js: CFG, teamColor, tyreIconHTML, setTyreIcon, KERS. */

const BATTLE_THRESH = 1.5;   // Abstand zum Vordermann < 1,5 s = "im Kampf"
const BATTLE_MAX    = 6;     // max. Fahrer in der Box
const BATTLE_ROW_H  = 44;    // Hoehe einer Battle-Zeile (px)

const gapSample = {};        // {idx:{t,gap}} fuer Schliessrate
let battlePred = null;

// Ueberhol-Projektion "Kampf in X Runden" (Setting `pred`)
function updateBattlePrediction(drivers, active) {
  if (!active) { battlePred = null; return; }
  const now = performance.now() / 1000;
  let best = null;
  drivers.forEach((d, i) => {
    if (i === 0 || d.in_pit || d.dnf || d.dsq) return;
    const g = d.gap_to_ahead, prev = gapSample[d.index];
    if (prev && now - prev.t > 1.2) {
      const rate = (prev.gap - g) / (now - prev.t);
      gapSample[d.index] = { t: now, gap: g };
      if (rate > 0.01 && g > 0.4 && g < 6) {
        const laps = g / (rate * (d.last_lap > 30 ? d.last_lap : 90));
        if (laps >= 1 && laps <= 9 && (!best || laps < best.laps))
          best = { laps: Math.round(laps), aIdx: drivers[i - 1].index, bIdx: d.index };
      }
    } else if (!prev) gapSample[d.index] = { t: now, gap: g };
  });
  battlePred = best;
}

function barStyle(gap) {
  const close = Math.max(0, Math.min(1, (1.0 - gap) / 1.0));   // erst < 1,0 s aktiv, voll bei 0
  return `width:${(close * 100).toFixed(0)}%;background:rgb(255,${Math.round(204 - 140 * close)},${Math.round(64 * close)})`;
}
function battleRowHTML(d, gap) {
  const tyre = tyreIconHTML(d.compound);
  const lead = gap == null;   // Spitze der Gruppe -> kein Vordermann
  const gapSpan = lead ? `<span class="bd-gap bd-gap-lead">—</span>` : `<span class="bd-gap">+${gap.toFixed(3)}</span>`;
  const bar = `<div class="gap-bar" style="${lead ? "display:none" : ""}"><div class="gap-bar-fill" style="${lead ? "" : barStyle(gap)}"></div></div>`;
  const name = (d.name || "").replace(/</g, "&lt;");
  return `<div class="battle-row" data-idx="${d.index}" style="--row-team:${teamColor(d.team)}">`
       + `<span class="bd-pos">${d.position}</span>`
       + `<span class="bd-name">${name}</span>`
       + `<span class="bd-right"><span class="bd-fresh"></span>${gapSpan}${tyre}</span>`   // Reifen + R3.3 Frische-Badge
       + bar + `</div>`;   // horizontaler Gap-Balken an der Unterkante
}

function setRowTyre(row, d) {
  setTyreIcon(row.querySelector(".bd-tyre"), d.compound);
}
function setRowGap(row, gap) {
  const el = row.querySelector(".bd-gap");
  const bar = row.querySelector(".gap-bar");
  if (gap == null) {
    if (el.textContent !== "—") el.textContent = "—";
    if (el.className !== "bd-gap bd-gap-lead") el.className = "bd-gap bd-gap-lead";
    if (bar) bar.style.display = "none";
    return;
  }
  el.textContent = "+" + gap.toFixed(3);
  if (el.className !== "bd-gap") el.className = "bd-gap";
  if (bar) { bar.style.display = ""; bar.querySelector(".gap-bar-fill").style.cssText = barStyle(gap); }
}

// R3.3: "frischere Reifen"-Badge am Jaeger (Auto direkt dahinter) in der Battle-Box.
const CMP_SOFT = { S: 0, M: 1, H: 2, I: 0, W: 1 };   // kleiner = weicher
function setRowFresh(row, d, ahead) {
  const el = row.querySelector(".bd-fresh");
  if (!el) return;
  let txt = "";
  if (CFG.fresh && ahead && !d.in_pit && !ahead.in_pit) {
    const diff = (ahead.tyre_age || 0) - (d.tyre_age || 0);              // + = Jaeger frischer
    const softer = (CMP_SOFT[d.compound] ?? 9) < (CMP_SOFT[ahead.compound] ?? 9);
    if (diff >= 5 || (softer && diff >= 3)) txt = "−" + diff + " Rnd";
  }
  el.textContent = txt;
  el.classList.toggle("show", !!txt);
}

function showBattleBox(box) {
  const wasHiding = box.dataset.hiding === "1";
  box.dataset.hiding = "0";
  box.classList.remove("out");
  if (box.style.display !== "block" || wasHiding) {
    box.style.display = "block";
    box.classList.remove("in"); void box.offsetWidth; box.classList.add("in");
  }
}
function hideBattleBox(box) {
  if (box.style.display === "none" || box.dataset.hiding === "1") return;
  box.dataset.hiding = "1";
  box.classList.remove("in");
  box.classList.add("out");
  setTimeout(() => {
    if (box.dataset.hiding === "1") {
      box.style.display = "none";
      box.classList.remove("out");
      box.dataset.hiding = "0";
      const rows = box.querySelector(".battle-rows");
      rows.innerHTML = ""; rows.dataset.members = "";
    }
  }, 700);
}

// Battle-Erkennung mit Hysterese + ruhiger Box-Verwaltung (kein hektisches
// Auf/Zu/Umsortieren mehr):
//  - Link entsteht bei < battlethresh, loest sich erst bei > battlethresh + 0,6 s
//  - neue Gruppen erscheinen erst nach 2 s Bestand (kein Aufblitzen bei Gap-Dips)
//  - Boxen behalten "ihre" Gruppe (Zuordnung ueber gemeinsame Fahrer, kein Slot-Shuffle)
//  - eine sichtbare Box bleibt mindestens 4 s stehen
const battleLink = {};    // followerIdx -> Link aktiv (Hysterese)
const groupSeen = {};     // Gruppen-Signatur -> erstes Auftreten
function findBattleGroups(drivers) {
  const groups = [];
  let cur = [];
  const enter = CFG.battlethresh || BATTLE_THRESH, exitG = enter + 0.6;
  const flush = () => { if (cur.length >= 2) groups.push(cur); };
  for (const d of drivers) {
    if (d.dnf || d.dsq || d.in_pit) { battleLink[d.index] = false; flush(); cur = []; continue; }
    if (cur.length === 0) { cur = [d]; continue; }
    const g = d.gap_to_ahead;
    const linked = g > 0 && (g < enter || (battleLink[d.index] && g < exitG));
    battleLink[d.index] = linked;
    if (linked) cur.push(d);
    else { flush(); cur = [d]; }
  }
  flush();
  return groups;
}

function renderBattleBox(box, g) {
  const front = g[0], back = g[g.length - 1];
  box.querySelector(".battle-header").textContent = g.length === 2
    ? `BATTLE FOR P${front.position}`
    : `BATTLE · P${front.position}–P${back.position}`;
  const sub = box.querySelector(".battle-sub");
  const inGroup = battlePred && g.some(d => d.index === battlePred.aIdx) && g.some(d => d.index === battlePred.bIdx);
  if (inGroup) {
    const laps = battlePred.laps;
    sub.innerHTML = `<span class="bs-ico">⚔</span><span class="bs-txt">Kampf in</span>`
      + `<span class="bs-laps${laps <= 1 ? " now" : ""}">${laps}</span>`
      + `<span class="bs-unit">${laps === 1 ? "Runde" : "Runden"}</span>`;
    if (sub.dataset.laps !== String(laps)) {        // bei Aenderung kurz "poppen"
      sub.dataset.laps = String(laps);
      const ln = sub.querySelector(".bs-laps");
      ln.classList.remove("bs-pop"); void ln.offsetWidth; ln.classList.add("bs-pop");
    }
    sub.classList.add("show");
  } else {
    sub.classList.remove("show");
    sub.textContent = "";
    sub.dataset.laps = "";
  }
  syncBattleRows(box.querySelector(".battle-rows"), g);
}

function updateBattleBox(drivers, connected) {
  const boxes = [...document.querySelectorAll("#battle-area .battle-box")];
  const now = performance.now();
  let groups = connected ? findBattleGroups(drivers) : [];
  groups.sort((a, b) => a[0].position - b[0].position);
  groups = groups.slice(0, boxes.length + 2).map(g => g.length > BATTLE_MAX ? g.slice(0, BATTLE_MAX) : g);
  // Reifezeit: Gruppe (Signatur = vorderste zwei Fahrer) muss 2 s bestehen, bevor sie erscheint
  const seenNow = new Set();
  const ready = [];
  groups.forEach(g => {
    const sig = g[0].index + "-" + g[1].index;
    seenNow.add(sig);
    if (!groupSeen[sig]) groupSeen[sig] = now;
    if (now - groupSeen[sig] >= 2000) ready.push(g);
  });
  for (const s in groupSeen) if (!seenNow.has(s)) delete groupSeen[s];
  // Sticky-Zuordnung: zuerst die Boxen bedienen, die diese Gruppe schon zeigen
  const assigned = new Array(boxes.length).fill(null);
  const usedGroups = new Set();
  const boxMembers = boxes.map(b => new Set((b.querySelector(".battle-rows").dataset.members || "")
    .split(",").filter(Boolean).map(Number)));
  ready.forEach(g => {
    let best = -1, bestOv = 1;    // mind. 2 gemeinsame Fahrer = "gleiche" Gruppe
    boxes.forEach((b, k) => {
      if (assigned[k] || b.style.display !== "block") return;
      const ov = g.filter(d => boxMembers[k].has(d.index)).length;
      if (ov > bestOv) { bestOv = ov; best = k; }
    });
    if (best >= 0) { assigned[best] = g; usedGroups.add(g); }
  });
  ready.forEach(g => {            // uebrige (neue) Gruppen in freie Boxen
    if (usedGroups.has(g)) return;
    let k = boxes.findIndex((b, i) => !assigned[i] && b.style.display !== "block");
    if (k < 0) k = assigned.findIndex(a => a === null);
    if (k >= 0) { assigned[k] = g; usedGroups.add(g); }
  });
  boxes.forEach((box, k) => {
    const g = assigned[k];
    if (g) {
      if (box.style.display !== "block") box.dataset.born = String(now);
      showBattleBox(box);
      renderBattleBox(box, g);
    } else {
      // Mindestanzeige: Box bleibt 4 s stehen (friert kurz ein), dann sauber raus
      const born = parseFloat(box.dataset.born || "0");
      if (connected && box.style.display === "block" && now - born < 4000) return;
      hideBattleBox(box);
    }
  });
}

function syncBattleRows(rowsEl, g) {
  rowsEl.style.height = (g.length * BATTLE_ROW_H) + "px";
  const memberSig = g.map(d => d.index).sort((a, b) => a - b).join(",");
  if (rowsEl.dataset.members !== memberSig) {
    // Andere Fahrer im Kampf -> sauber neu aufbauen (sonst ueberlappen alte/neue Zeilen).
    rowsEl.dataset.members = memberSig;
    rowsEl.innerHTML = "";
    g.forEach((d, i) => {
      rowsEl.insertAdjacentHTML("beforeend", battleRowHTML(d, i === 0 ? null : d.gap_to_ahead));
      const row = rowsEl.lastElementChild;
      setRowFresh(row, d, i === 0 ? null : g[i - 1]);
      row.classList.add("entering");
      row.style.transition = "none";
      row.style.transform = `translateY(${i * BATTLE_ROW_H}px)`;
      void row.offsetWidth;
      row.style.transition = "";
      requestAnimationFrame(() => requestAnimationFrame(() => row.classList.remove("entering")));
    });
  } else {
    // Gleiche Fahrer -> Position smooth verschieben (CSS-Transition) + Werte aktualisieren.
    g.forEach((d, i) => {
      const row = rowsEl.querySelector(`[data-idx="${d.index}"]`);
      if (!row) return;
      row.style.transform = `translateY(${i * BATTLE_ROW_H}px)`;
      row.querySelector(".bd-pos").textContent = d.position;
      row.style.setProperty("--row-team", teamColor(d.team));
      setRowTyre(row, d);
      setRowGap(row, i === 0 ? null : d.gap_to_ahead);
      setRowFresh(row, d, i === 0 ? null : g[i - 1]);
    });
  }
}

/* ── Einhaengen ────────────────────────────────────────────────────────────────
   Bedingungen wie in test.html Z. 2284 + 2307:
   - Projektion: CFG.pred, verbunden, kein Quali
   - Boxen: Regie-Schalter, CFG.battles, verbunden, kein Quali, ab Runde 3
   KERS.on("battles") ist auf der Einzelseite immer true (ausser ?respect=1). */
KERS.onData((data, drivers) => {
  const s = data.session || {};
  updateBattlePrediction(drivers, CFG.pred && data.connected && !s.is_quali);
  updateBattleBox(drivers, KERS.regie.battles && KERS.on("battles") && data.connected
                           && !s.is_quali && (s.current_lap || 0) >= 3);
});
