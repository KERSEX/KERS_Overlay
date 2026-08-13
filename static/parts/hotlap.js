/* Quali-Hotlap-Boxen — 1:1 aus templates/test.html (Z. 2548–2666).
   Zeigt je Fahrer auf On-Track/Hotlap: laufende Zeit, Live-Delta zu den
   Session-Bestsektoren und die Live-Sektoren (farbcodiert).

   ⚠ secBest/drvSecBest kommen aus core.js (deriveShared) — im Gesamt-Overlay
   werden sie in der Tower-Schleife gepflegt, siehe Kommentar test.html Z. 2572. */

function hideHotlapBox(box) {
  if (box.style.display === "none" || box.dataset.hiding === "1") return;
  box.dataset.hiding = "1";
  box.classList.remove("in");
  box.classList.add("out");
  setTimeout(() => {
    if (box.dataset.hiding === "1") {
      box.style.display = "none";
      box.classList.remove("out");
      box.dataset.hiding = "0";
      box.dataset.idx = "";
    }
  }, 450);
}
function showHotlapBox(box) {
  const wasHiding = box.dataset.hiding === "1";
  box.dataset.hiding = "0";
  box.classList.remove("out");
  if (box.style.display !== "block" || wasHiding) {
    box.style.display = "block";
    box.classList.remove("in"); void box.offsetWidth; box.classList.add("in");
  }
}
// Sektor-Farbe wie im Tower (lila = Session-Best, gruen = persoenlich, gelb = langsamer).
// Nur lesend: secBest/drvSecBest werden vorher in deriveShared() gepflegt.
function hlSecClass(v, idx, i) {
  if (!(v > 0)) return "";
  const pb = drvSecBest[idx];
  if (v <= secBest[i] + 0.005) return "sp";
  if (pb && v <= pb[i] + 0.005) return "sg";
  return "sy";
}
function buildHotlapSkeleton(box) {
  box.innerHTML =
      `<div class="hl-head"><span class="hl-dot"></span><span class="hl-title">Hot Lap</span>`
    + `<span class="hl-pos"></span><img class="hl-tyre" alt=""></div>`
    + `<div class="hl-body">`
    +   `<div class="hl-namerow"><img class="hl-logo" alt=""><span class="hl-name"></span></div>`
    +   `<div class="hl-timerow"><span class="hl-time"></span><span class="hl-delta"></span></div>`
    +   `<div class="hl-sectors">`
    +     `<div class="hl-sec"><div class="lbl">S1</div><div class="val s1">—</div></div>`
    +     `<div class="hl-sec"><div class="lbl">S2</div><div class="val s2">—</div></div>`
    +     `<div class="hl-sec"><div class="lbl">S3</div><div class="val s3">—</div></div>`
    +   `</div>`
    + `</div>`;
}
function updateHotlapBox(box, d) {
  box.style.setProperty("--hl-team", teamColor(d.team));
  if (box.dataset.idx !== String(d.index)) { box.dataset.idx = String(d.index); buildHotlapSkeleton(box); }
  box.querySelector(".hl-pos").textContent = "P" + d.position;
  box.querySelector(".hl-name").textContent = d.name;
  setTeamLogo(box.querySelector(".hl-logo"), d.team);
  setTyreIcon(box.querySelector(".hl-tyre"), d.compound);
  box.classList.toggle("invalid", !!d.lap_invalid);

  // s3 erst bekannt, wenn die Runde komplett ist (last_lap - s1 - s2)
  const s3 = (d.last_lap > 0 && d.sector1 > 0 && d.sector2 > 0) ? d.last_lap - d.sector1 - d.sector2 : 0;
  const secs = [d.sector1, d.sector2, s3];

  // Grosse Zeit = LIVE hochzaehlende Rundenzeit. Basiswert + Zeitstempel merken; der
  // rAF-Loop tickHotlapTimes() zaehlt zwischen den Datenupdates fluessig weiter.
  let done = 0;
  secs.forEach(v => { if (v > 0) done++; });   // Anzahl fertiger Sektoren (fuers Delta unten)
  const clt = d.current_lap_time || 0;
  if (clt > 0) {
    box.dataset.tBase = String(clt);
    box.dataset.tAt = String(performance.now());
    box.dataset.tick = "1";
  } else {
    box.dataset.tick = "0";
    box.querySelector(".hl-time").textContent = d.last_lap > 0 ? fmtTime(d.last_lap) : "—";
  }

  // Live-Delta gegen die Session-Bestsektoren bis zum aktuellen Sektor
  let delta = 0, haveDelta = false;
  for (let i = 0; i < done; i++) { if (secBest[i] < Infinity) { delta += secs[i] - secBest[i]; haveDelta = true; } }
  const dEl = box.querySelector(".hl-delta");
  if (haveDelta) {
    const up = delta <= 0.0005;
    dEl.textContent = (up ? "−" : "+") + Math.abs(delta).toFixed(3);
    dEl.className = "hl-delta " + (up ? "up" : "down");
  } else { dEl.textContent = ""; dEl.className = "hl-delta"; }

  ["s1", "s2", "s3"].forEach((cls, i) => {
    const el = box.querySelector(".hl-sec .val." + cls);
    el.textContent = fmtTime(secs[i]) || "—";
    el.className = "val " + cls + " " + hlSecClass(secs[i], d.index, i);
  });
}
function updateHotlapBoxes(drivers, active) {
  const boxes = document.querySelectorAll("#hotlap-area .hotlap-box");
  // Fahrer auf fliegender Runde (On-Track/Hotlap), sortiert nach Streckenfortschritt:
  // wer als Naechstes ueber die Linie kommt steht zuerst.
  let hot = [];
  if (active) {
    hot = drivers.filter(d => qualiStatus(d) === "track" && !d.dnf && !d.dsq)
                 .sort((a, b) => (b.lap_distance || 0) - (a.lap_distance || 0)).slice(0, boxes.length);
  }
  boxes.forEach((box, k) => {
    const d = hot[k];
    if (!d) { hideHotlapBox(box); return; }
    showHotlapBox(box);
    updateHotlapBox(box, d);
  });
}

// Hotlap-Grosszeit fluessig hochzaehlen (rAF interpoliert zwischen den Datenupdates)
function tickHotlapTimes() {
  document.querySelectorAll("#hotlap-area .hotlap-box").forEach(box => {
    if (box.style.display === "none" || box.dataset.tick !== "1") return;
    const base = parseFloat(box.dataset.tBase || "0");
    const at   = parseFloat(box.dataset.tAt || "0");
    const el = box.querySelector(".hl-time");
    if (el) el.textContent = fmtTime(base + (performance.now() - at) / 1000) || "—";
  });
  requestAnimationFrame(tickHotlapTimes);
}
requestAnimationFrame(tickHotlapTimes);

// Bedingung wie in test.html Z. 2308: Regie-Schalter, verbunden, Quali, kein Ergebnis
KERS.onData((data, drivers) => {
  const s = data.session || {};
  updateHotlapBoxes(drivers, KERS.regie.hotlap && data.connected && s.is_quali && !s._quali_result);
});
