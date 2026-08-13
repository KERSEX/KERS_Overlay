/* Start-Ampel — 1:1 aus templates/test.html (Z. 2703–2723).
   STLG meldet die Anzahl leuchtender Lichter (1..5), LGOT = alle aus (Start frei).
   age = Sekunden seit dem letzten Event -> nach ein paar Sekunden ausblenden. */

const slEl = document.getElementById("start-lights");
const slLamps = () => slEl.querySelectorAll(".sl-lamp");

function updateStartLights(sl, connected) {
  if (!KERS.on("lights") || !sl || !connected) { slEl.classList.remove("show", "lightsout"); return; }
  const { num, out, age } = sl;
  if (out) {
    // "Lights Out" 3 s lang zeigen, dann weg
    if (age < 3.0) { slEl.classList.add("show", "lightsout"); }
    else slEl.classList.remove("show", "lightsout");
  } else if (num > 0 && age < 8.0) {
    // Lichter gehen nacheinander an (Saeulen von links)
    slEl.classList.add("show");
    slEl.classList.remove("lightsout");
    slLamps().forEach((l, i) => l.classList.toggle("on", Math.floor(i / 2) < num));
  } else {
    slEl.classList.remove("show", "lightsout");
  }
}

KERS.onData(data => updateStartLights(data.start_lights, data.connected));
