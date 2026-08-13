/* Onboard-Telemetrie — 1:1 aus templates/test.html (Z. 2735–2819).
   Braucht aus core.js: CFG, teamColor, setTeamLogo, qualiStatus,
   drvSecBest, secBest, curS3, scOnboardActive, scRestartUntilLap, KERS.

   ⚠ scOnboardActive/scRestartUntilLap werden in core.js (deriveShared) gesetzt —
   im Gesamt-Overlay passiert das in render(). Hier wird scRestartUntilLap unten
   auch wieder ZURUECKgesetzt, genau wie im Original. */

const obEl = document.getElementById("onboard");

function updateOnboard(drivers, focusIdx, active, session) {
  const raceVisible = scOnboardActive || scRestartUntilLap > 0;
  // Quali: braucht einen Kamera-Fahrer. Rennen: nur im Safety-Car-Fenster sichtbar
  // (dann IMMER P1, egal auf wen die Kamera schaut).
  const visible = KERS.on("onboard") && active
                  && (session.is_quali ? (focusIdx != null && focusIdx >= 0) : raceVisible);
  if (!visible) { obEl.classList.remove("show"); return; }

  let d;
  if (session.is_quali) {
    d = drivers.find(x => x.index === focusIdx);
  } else {
    // Rennen im SC-Fenster: Onboard zeigt IMMER P1. Fenster schliesst erst, wenn das SC weg
    // IST und P1 ueber Start/Ziel gefahren ist (scRestartUntilLap wird bei SC-Ende gesetzt).
    d = drivers.find(x => x.position === 1);
    if (scRestartUntilLap > 0 && d && (d.lap_num || 0) >= scRestartUntilLap) {
      scRestartUntilLap = 0; scOnboardActive = false;
      obEl.classList.remove("show"); return;
    }
  }
  if (!d) { obEl.classList.remove("show"); return; }
  obEl.classList.add("show");
  obEl.style.setProperty("--ob-team", teamColor(d.team));
  setTeamLogo(obEl.querySelector(".ob-logo"), d.team);
  obEl.querySelector(".ob-name").textContent = d.name;
  obEl.querySelector(".ob-pos").textContent = "P" + d.position;
  obEl.querySelector(".ob-speed").textContent = d.speed || 0;
  const g = d.gear;
  obEl.querySelector(".ob-gear").textContent = g === -1 ? "R" : g === 0 ? "N" : g;
  obEl.querySelector(".ob-bar.thr .fill").style.width = ((d.throttle || 0) * 100).toFixed(0) + "%";
  obEl.querySelector(".ob-bar.brk .fill").style.width = ((d.brake || 0) * 100).toFixed(0) + "%";

  // N3: Live-Delta zur persoenlichen Bestrunde — NUR in der Quali und NUR wenn der
  // Fokus-Fahrer auf einem Hotlap ist (im Rennen kein Delta).
  const dRow = obEl.querySelector(".ob-delta");
  if (dRow) {
    const onHotlap = session.is_quali && qualiStatus(d) === "track";
    if (!CFG.deltabar || !onHotlap) { dRow.style.display = "none"; }
    else {
      dRow.style.display = "";
      const pb = drvSecBest[d.index];
      const done = Math.min(d.sector || 0, 2);   // m_sector: 0=S1 laeuft, 1=S1 fertig, 2=S1+S2 fertig
      let delta = 0, have = false;
      if (pb) {
        const secs = [d.sector1, d.sector2];
        for (let i = 0; i < done; i++)
          if (secs[i] > 0 && pb[i] < Infinity) { delta += secs[i] - pb[i]; have = true; }
      }
      const fill = dRow.querySelector(".fill"), val = dRow.querySelector(".ob-dval");
      if (!have) { fill.style.width = "0"; val.textContent = "—"; val.style.color = "#9aa0aa"; }
      else {
        const pct = Math.min(Math.abs(delta) / 0.75, 1) * 50;   // ±0,75 s = voller Ausschlag
        fill.style.left = (delta <= 0 ? 50 - pct : 50) + "%";
        fill.style.width = pct + "%";
        fill.style.background = delta <= 0 ? "linear-gradient(90deg,#0a8f3c,#27e06a)" : "linear-gradient(90deg,#ff3b30,#a10f0f)";
        val.textContent = (delta <= 0 ? "−" : "+") + Math.abs(delta).toFixed(3);
        val.style.color = delta <= 0 ? "#27e06a" : "#ff5a5a";
      }
    }
  }

  // I34: Live-Sektor-Ampel — 3 Segmente, gefaerbt gegen Session-Best (lila) / persoenl.
  // Bestzeit (gruen) / langsamer (gelb). Nur Quali auf dem Hotlap (wie der Delta-Balken).
  // S1/S2 live; S3 aus der zuletzt beendeten Runde (curS3); laufendes Segment mit Rahmen.
  const ampRow = obEl.querySelector(".ob-amp");
  if (ampRow) {
    const showAmp = session.is_quali && qualiStatus(d) === "track" && CFG.ampel !== false;
    ampRow.style.display = showAmp ? "" : "none";
    if (showAmp) {
      const pb = drvSecBest[d.index];
      const done = Math.min(d.sector || 0, 2);
      const secs = [d.sector1 || 0, d.sector2 || 0, (d.sector >= 2 ? 0 : (curS3[d.index] || 0))];
      const segs = ampRow.querySelectorAll(".ob-amp-seg");
      secs.forEach((v, i) => {
        segs[i].className = "ob-amp-seg";
        if (v > 0) {
          const pbv = pb ? pb[i] : Infinity;
          segs[i].classList.add(v <= secBest[i] + 0.005 ? "sp" : (v <= pbv + 0.005 ? "sg" : "sy"));
        } else if (i === done) {
          segs[i].classList.add("run");
        }
      });
    }
  }
}

KERS.onData((data, drivers) => {
  updateOnboard(drivers, data.focus_index, data.connected, data.session || {});
});
