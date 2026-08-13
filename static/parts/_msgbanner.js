/* Gemeinsamer Meldungs-Banner — 1:1 aus templates/test.html (Z. 1710–1730).
   Wird von racemsg.js UND undercut.js benutzt: beide zeigen denselben Banner,
   nur mit unterschiedlicher Quelle. Deshalb liegt er in einer eigenen Datei
   (Unterstrich im Namen = kein eigener Baustein, nur ein Baustein-Bauteil). */

let rcQueue = [], rcBusy = false;

// Rennleitung: ein Meldungs-Feed oben, der Events nacheinander durchwechselt.
function raceMsg(icon, html, accent) {
  if (!KERS.on("msgs")) return;               // Meldungen per Settings abschaltbar
  rcQueue.push({ icon, html, accent });
  if (rcQueue.length > 10) rcQueue.shift();   // Stau begrenzen
  processRaceMsg();
}
function processRaceMsg() {
  if (rcBusy || !rcQueue.length) return;
  rcBusy = true;
  const m = rcQueue.shift();
  const el = document.getElementById("race-msg");
  el.style.setProperty("--rm-accent", m.accent);
  el.querySelector(".rm-icon").textContent = m.icon;
  el.querySelector(".rm-text").innerHTML = m.html;
  el.classList.add("show");
  setTimeout(() => {
    el.classList.remove("show");
    setTimeout(() => { rcBusy = false; processRaceMsg(); }, 450);   // kurze Pause -> naechste Meldung
  }, 2800);
}
