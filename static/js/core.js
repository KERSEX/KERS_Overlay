/* ═══════════════════════════════════════════════════════════════════════════════
   KERS F1 Overlay — gemeinsamer Unterbau für die Bausteinseiten (/part/<name>)

   Enthält alles, was JEDER Baustein braucht: Settings, Team-/Reifen-Tabellen,
   Zeitformate, Branding, Regie, die SSE-Anbindung — und `deriveShared()`.

   ⚠ Warum `deriveShared()` existiert:
   Im Gesamt-Overlay (test.html) pflegt die grosse Tower-Schleife in `render()`
   nebenbei Zustand, den ANDERE Bausteine nur noch LESEN. Der Kommentar in
   test.html Z. 2572 sagt es direkt: "Nur lesend: secBest/drvSecBest werden vorher
   in der Tower-Schleife gepflegt." Laeuft onboard.html/hotlap.html/lowerthird.html
   allein, gibt es diese Schleife nicht — die Buchhaltung muss also hierher.

   Betrifft: secBest, drvSecBest, bestLapSecs, prevLastLap, lastQualiSecs, curS3
             (Onboard-Delta + Sektor-Ampel I34, Hotlap-Sektorfarben)
             stintLen/prevComp/prevAge (Stint-Chips im Lower-Third)
             scOnboardActive/scRestartUntilLap (Onboard-Fenster bei Safety Car)
             gapHist/lapHist (Gap-/Rundenzeiten-Chart)

   Die Funktionen selbst sind 1:1 aus test.html uebernommen. test.html bleibt
   unveraendert — dort laeuft weiterhin der Originalcode.
   ═══════════════════════════════════════════════════════════════════════════════ */

/* ── Konfiguration ───────────────────────────────────────────────────────────────
   Basis = Server-Settings (live ueber /settings aenderbar, kommen mit jedem Payload).
   URL-Parameter ueberschreiben einzelne Werte pro Browser-Quelle
   (z.B. /part/tower?rows=10&scale=0.8 fuer eine zweite OBS-Szene). */
const DEFAULT_CFG = {
  tower: true, battles: true, map: true, onboard: true, lights: true, damage: true,
  msgs: true, ticker: true, lowerthird: true, flbanner: true,
  undercut: true, deltabar: true, mapnumbers: true, mapflags: true,
  pred: true, danger: true, comeback: true, pbflash: true, fresh: true, strat: true, pitproj: true,
  brand_title: "", brand_accent: "#e10600", header_color: "", row_color: "",
  tower_logo: "", tower_logo_pos: "left", tower_logo_h: 34,
  scale: 0, rows: 0, mapsize: 400, maprot: 110, mapflip: true, dotsize: 1.0,
  holds: 300, ltdur: 4.0, flbdur: 4.5, dmgcrit: 60, battlethresh: 1.5, preset: "voll",

  // ⚠ Diese beiden stehen in main.py DEFAULT_SETTINGS und in overlay_settings.json,
  //   fehlen aber im DEFAULT_CFG von test.html. Da applySettings() nur ueber die Keys
  //   von DEFAULT_CFG laeuft, sind `ampel` und `mapcorner` im Gesamt-Overlay
  //   WIRKUNGSLOS: CFG.ampel ist dort immer undefined -> die Pruefung
  //   `CFG.ampel !== false` (test.html Z. 2801) ist immer wahr, die Sektor-Ampel laesst
  //   sich also gar nicht abschalten. Hier sind die Settings korrekt verdrahtet.
  ampel: true,
  mapcorner: "tr",

  // Deckkraft der Panels: Faktor auf die Alpha-Werte in core.css (--ui-alpha).
  // 1 = wie bisher, kleiner = durchsichtiger, ab ~1.25 komplett deckend.
  opacity: 1.0,
};
const QP = new URLSearchParams(location.search);
const urlOverride = {};
QP.forEach((v, k) => {
  k = k.toLowerCase();
  if (!(k in DEFAULT_CFG)) return;
  const d = DEFAULT_CFG[k];
  urlOverride[k] = typeof d === "boolean" ? (v !== "0" && v !== "false")
                 : typeof d === "number" ? (parseFloat(v) || 0) : v;
});
const CFG = Object.assign({}, DEFAULT_CFG);
function applySettings(s) {
  for (const k in DEFAULT_CFG)
    CFG[k] = (k in urlOverride) ? urlOverride[k] : (s && s[k] !== undefined ? s[k] : DEFAULT_CFG[k]);
}
applySettings(null);

/* Auf einer Einzelseite ist der eigene Baustein IMMER an — sonst muesste man in den
   Settings alles anlassen, nur damit die OBS-Quelle etwas zeigt. Die Sichtbarkeits-
   Schalter (CFG.tower, CFG.battles, ...) gehoeren zum Gesamt-Overlay. Wer sie doch
   respektiert haben will, haengt ?respect=1 an die URL. */
const PART_FORCE_ON = QP.get("respect") !== "1";

const TEAM_COLORS = {
  "Red Bull":     "var(--team-redbull)",
  "Ferrari":      "var(--team-ferrari)",
  "McLaren":      "var(--team-mclaren)",
  "Mercedes":     "var(--team-mercedes)",
  "Aston Martin": "var(--team-aston)",
  "Haas":         "var(--team-haas)",
  "RB":           "var(--team-rb)",
  "Alpine":       "var(--team-alpine)",
  "Williams":     "var(--team-williams)",
  "Sauber":       "var(--team-sauber)",
  "Audi":         "var(--team-audi)",
  "Cadillac":     "var(--team-cadillac)",
};
const teamColor = team => TEAM_COLORS[team] || "#fff";

// Team-Logos in static/teams/ (Dateinamen mit Unterstrich). RB -> Racing_Bull, Sauber -> Audi (2026).
const TEAM_LOGOS = {
  "Red Bull": "Red_Bull_Half", "Ferrari": "Ferrari", "McLaren": "McLaren", "Mercedes": "Mercedes",
  "Aston Martin": "Aston_Martin", "Haas": "Haas", "RB": "Racing_Bull", "Alpine": "Alpine",
  "Williams": "Williams", "Sauber": "Audi", "Audi": "Audi", "Cadillac": "Cadillac",
};
function setTeamLogo(img, team) {
  const f = TEAM_LOGOS[team];
  if (f) {
    const src = `static/teams/${f}.png`;
    if (img.getAttribute("src") !== src) img.src = src;
    img.style.visibility = "visible";
  } else { img.removeAttribute("src"); img.style.visibility = "hidden"; }
}
const TYRE_COLOR = { S:"#E8002D", M:"#FFC906", H:"#EBEBEB", I:"#39B54A", W:"#0067FF", E:"#00CFFF", "?":"#555" };
const TYRE_ICONS = { S:1, M:1, H:1, I:1, W:1 };   // static/tyres/<X>.png
function setTyreIcon(img, comp) {
  if (TYRE_ICONS[comp]) {
    const src = `static/tyres/${comp}.png`;
    if (img.getAttribute("src") !== src) {
      const hadTyre = !!img.getAttribute("src");   // war schon ein Reifen drauf?
      img.src = src;
      img.style.visibility = "visible";
      if (hadTyre) {   // echter Wechsel (Boxenstopp) -> neuer Reifen dreht sich rein
        img.classList.remove("tyre-spin"); void img.offsetWidth; img.classList.add("tyre-spin");
      }
    }
  } else { img.removeAttribute("src"); img.style.visibility = "hidden"; }
}
const tyreIconHTML = comp => TYRE_ICONS[comp]
  ? `<img class="bd-tyre" src="static/tyres/${comp}.png" alt="${comp}">`
  : `<img class="bd-tyre" alt="" style="visibility:hidden">`;
const pitTyreImg = (c, cls = "") => TYRE_ICONS[c] ? `<img class="${cls}" src="static/tyres/${c}.png" alt="${c}">` : "";

const esc = s => (s || "").replace(/</g, "&lt;");

function fmtTime(s) {
  if (!s || s <= 0) return null;
  const m = Math.floor(s / 60);
  const sec = (s % 60).toFixed(3).padStart(6, "0");
  return m > 0 ? `${m}:${sec}` : sec;
}

function fmtGap(s) {
  if (!s || s <= 0) return "0.000";
  return `+${s.toFixed(3)}`;
}

/* ── Geteilte Buchhaltung (im Gesamt-Overlay Teil der Tower-Schleife) ─────────── */
let secBest = [Infinity, Infinity, Infinity];   // Session-Bestzeit pro Sektor (Quali)
const drvSecBest = {};                          // Bestzeit pro Fahrer pro Sektor
const bestLapSecs = {};                         // die 3 Sektoren der SCHNELLSTEN Runde je Fahrer
const prevLastLap = {};                         // letzte gesehene Rundenzeit (Runden-Ende-Erkennung)
const lastQualiSecs = {};                       // {idx:[s1,s2]} zuletzt gueltige S1/S2 -> S3 der beendeten Runde
const curS3 = {};                               // I34: S3 der zuletzt beendeten Runde (fuers Onboard-Ampel-Segment)
let lastSecType = null;                         // Session-Typ-Wechsel -> Bests reset
const stintLen = {}, prevComp = {}, prevAge = {};   // Stint-Laengen je Fahrer (Lower-Third)
let scOnboardActive = false;                    // Rennen: Onboard-Fenster (SC bis Restart-Ueberfahrt)
let scRestartUntilLap = 0;                      // SC-Restart: Onboard zeigt P1, bis P1 ueber Start/Ziel ist
let lastScStatus = null;                        // Statuswechsel-Erkennung
let gapHist = {}, gapLastLap = 0;               // Gap-Verlauf je Runde (Chart G)
let lapHist = {};                               // Rundenzeit je Runde/Fahrer (Chart L)
let lastDrivers = [];                           // letzter Fahrer-Stand (fuer Chart-Meta)
let lastLapCount = 0;

// Bestsektoren vom SPIEL (Packet 11) in die eigene Buchhaltung uebernehmen. Ohne das kennt
// das Overlay nur Sektoren, die es selbst mitgehoert hat — alles vor dem Start fehlt.
// Zusammengefuehrt per Minimum, damit eine gerade live gesehene Bestzeit nicht verloren geht.
function mergeBestSectors(d) {
  const bs = d.best_sectors;
  if (!bs) return;
  const pb = drvSecBest[d.index] || (drvSecBest[d.index] = [Infinity, Infinity, Infinity]);
  for (let i = 0; i < 3; i++) {
    const v = bs[i];
    if (!(v > 0)) continue;
    if (v < pb[i]) pb[i] = v;
    if (v < secBest[i]) secBest[i] = v;      // Session-Bestsektor (lila) ueber alle Fahrer
  }
}

// Persoenliche Bestsektoren auch im Rennen mitfuehren (Grundlage fuer den Delta-Balken)
function updateRaceBest(idx, secs) {
  const pb = drvSecBest[idx] || (drvSecBest[idx] = [Infinity, Infinity, Infinity]);
  secs.forEach((v, i) => { if (v > 0 && v < pb[i]) pb[i] = v; });
}

// Das, was `colorSectors()` in test.html NEBEN dem Einfaerben tut (Z. 1584/1585):
// live gefahrene Sektoren in Session- und Fahrer-Bestzeit einrechnen. Ohne DOM.
function absorbSectors(idx, secs) {
  const pb = drvSecBest[idx] || (drvSecBest[idx] = [Infinity, Infinity, Infinity]);
  secs.forEach((v, i) => {
    if (!(v > 0)) return;
    if (v < secBest[i]) secBest[i] = v;
    if (v < pb[i]) pb[i] = v;
  });
}

// Quali-Status eines Fahrers: box / out(lap) / inlap / track(=Hotlap).
// STABIL gehalten (kein Flackern -> Fahrer/Delta bleiben in der Hotlap-Box):
//   (1) Ungueltige Runde (Track Limits) -> NIE Hotlap.
//   (2) Klar zu langsam (verbummelte/abgebrochene Runde) -> inlap, raus aus den Boxen.
//  (2a) ERS ENTLASTET: wer deployt oder noch Ladung hat, faehrt keine Auslaufrunde.
//       Wenig ERS heisst dagegen NIE inlap - das entscheidet allein das Delta.
//   (3) Nachweislich auf Bestpace (fertiger Sektor nahe Bestsektor) -> Hotlap.
//   (4) Frischer Rundenstart (S1 noch nicht gepostet, kein Beweis): fliegende Runde laut
//       Spiel (ds=1) ODER aktives ERS-Deploy (Modus Hotlap/Overtake) ODER >=40 % ERS -> er
//       greift an -> Hotlap.
function qualiStatus(d) {
  const ds = d.driver_status;
  let st = d.in_pit ? "box" : ds === 3 ? "out" : ds === 2 ? "inlap"
         : (ds === 1 || ds === 4) ? "track" : "box";
  if (st !== "track") return st;

  // (1) Ungueltige Runde ist nie ein Hotlap.
  if (d.lap_invalid) return "inlap";

  const pb  = drvSecBest[d.index];
  const clt = d.current_lap_time || 0;
  const TOL = 1.4;   // Pace-Toleranz (etwas grosszuegiger -> weniger Fehl-Abbrueche/Flackern)

  let tooSlow = false;   // klar langsamer als die Bestpace -> Runde hin
  let onPace  = false;   // fertiger Sektor nahe der Bestpace -> bewiesen schnell
  if (pb) {
    // Fertige Sektoren dieser Runde gegen die persoenlichen Bestsektoren.
    const s3 = (d.last_lap > 0 && d.sector1 > 0 && d.sector2 > 0) ? d.last_lap - d.sector1 - d.sector2 : 0;
    let ab = 0, have = false;
    [d.sector1, d.sector2, s3].forEach((v, i) => { if (v > 0 && pb[i] < Infinity) { ab += v - pb[i]; have = true; } });
    if (have) { if (ab > TOL) tooSlow = true; else onPace = true; }
    // LIVE, schon MITTEN im Sektor ueber der Bestpace bis zum Sektorende -> Runde hin.
    if (clt > 0.5) {
      let cum = 0, ok = true;
      for (let i = 0; i <= Math.min(d.sector || 0, 2); i++) { if (pb[i] < Infinity) cum += pb[i]; else ok = false; }
      if (ok && clt > cum + TOL) tooSlow = true;
    }
  }

  // (2a) ERS entlastet. Verglichen wird gegen die SUMME der persoenlichen
  // Bestsektoren, und die stammen aus verschiedenen Runden - diese theoretische
  // Bestzeit faehrt niemand je. Jede echte Runde liegt systematisch darueber,
  // auch eine sehr gute; deshalb konnte sogar eine Runde, mit der sich der
  // Fahrer VERBESSERT, ueber die Toleranz rutschen und als abgebrochen gelten.
  // ⚠ Muss zu quali_status in hud/derive.py passen - die Datei ist die
  // Portierung dieser Funktion, und zwei auseinandergelaufene Fassungen zeigen
  // im HUD etwas anderes an als in OBS.
  if (tooSlow && greiftAn(d)) tooSlow = false;

  if (tooSlow) return "inlap";                         // (2) klar zu langsam
  if (onPace)  return "track";                         // (3) bewiesen schnell
  // (4) Frischer Start ohne Sektor-Beweis: fliegende Runde / am Deployen / genug ERS -> Hotlap.
  if (ds === 1 || greiftAn(d)) return "track";
  return "out";
}

// Deployt der Fahrer gerade oder haelt er noch ordentlich Ladung?
// m_ersDeployMode: 0=keins, 1=mittel, 2=Hotlap, 3=Overtake.
const ERS_MODE_AN = 2, ERS_PCT_AN = 40;
function greiftAn(d) {
  return (d.ers_mode || 0) >= ERS_MODE_AN || (d.ers_pct || 0) >= ERS_PCT_AN;
}

// Stint-Laengen mitschreiben: aktueller Reifen = tyre_age; bei Compound-Wechsel den vorigen
// Stint mit seiner erreichten Rundenzahl (letztes tyre_age vor dem Wechsel) sichern.
function trackStints(drivers) {
  drivers.forEach(d => {
    const c = d.compound;
    if (!c || !"SMHIWE".includes(c)) return;
    const pc = prevComp[d.index];
    if (pc !== undefined && pc !== c) {
      (stintLen[d.index] || (stintLen[d.index] = [])).push({ c: pc, laps: prevAge[d.index] || 0 });
      if (stintLen[d.index].length > 6) stintLen[d.index].shift();
    }
    prevComp[d.index] = c;
    prevAge[d.index] = d.tyre_age || 0;
  });
}

/* ── Ergebnis stehen lassen (Quali -> Rennen) ─────────────────────────────────── */
let qSnap = null, qLiveDrv = null, qLiveSes = null, qWasQuali = false, qSince = 0;
function maybeHoldQuali(data) {
  const s = data.session || {};
  const now = performance.now();
  if (data.connected && s.is_quali && (data.drivers || []).length) {
    qLiveDrv = data.drivers; qLiveSes = s; qWasQuali = true; qSnap = null;
    return data;
  }
  if (qWasQuali && !s.is_quali && !qSnap && qLiveDrv) { qSnap = qLiveDrv; qSince = now; }
  if (qSnap) {
    const sl = data.start_lights || {};
    const lights = (sl.num > 0 || sl.out) && sl.age < 30;                 // Rennstart laeuft
    const racing = data.connected && !s.is_quali && (s.current_lap || 0) >= 1;
    const expired = (now - qSince) > (CFG.holds || 300) * 1000;
    if (lights || racing || expired) { qSnap = null; qWasQuali = false; return data; }
    return Object.assign({}, data, { drivers: qSnap, connected: true, focus_index: -1, race_control: [],
      session: Object.assign({}, qLiveSes, { _quali_result: true, safety_car_status: "none", time_left: 0 }) });
  }
  return data;
}

/* ── Branding: Akzentfarbe + Titel + Tower-Logo aus den Settings ──────────────── */
let _brandSig = "";
function applyBrand() {
  const sig = [CFG.brand_accent, CFG.brand_title, CFG.header_color, CFG.row_color,
               CFG.tower_logo, CFG.tower_logo_pos, CFG.tower_logo_h, CFG.opacity].join("|");
  if (sig === _brandSig) return;
  _brandSig = sig;
  const hexOk = v => v && /^#[0-9a-fA-F]{3,8}$/.test(v), root = document.documentElement.style;
  if (hexOk(CFG.brand_accent)) root.setProperty("--accent-red", CFG.brand_accent);
  // Deckkraft: Faktor auf die Alpha-Werte aller Panel-Flaechen (--ui-alpha in core.css).
  // Untergrenze 0.2, damit man sich das Overlay nicht komplett wegdreht. Identisch zu test.html.
  const op = (CFG.opacity > 0 ? CFG.opacity : 1);
  root.setProperty("--ui-alpha", String(Math.max(0.2, Math.min(1.25, op))));
  if (hexOk(CFG.header_color)) root.setProperty("--header-accent", CFG.header_color); else root.removeProperty("--header-accent");
  if (hexOk(CFG.row_color)) root.setProperty("--row-override", CFG.row_color); else root.removeProperty("--row-override");
  // Die beiden Elemente gibt es nur auf der Tower-Seite -> ueberall sonst still uebersprungen.
  const bt = document.getElementById("brandTitle");
  if (bt) { bt.textContent = CFG.brand_title || ""; bt.style.display = CFG.brand_title ? "" : "none"; }
  const bl = document.getElementById("brandLogo");
  if (bl) {
    const file = (CFG.tower_logo || "").trim();
    bl.classList.toggle("show", !!file);
    bl.classList.toggle("right", CFG.tower_logo_pos === "right");
    bl.classList.toggle("left", CFG.tower_logo_pos !== "right");
    if (file) {
      const src = "static/logos/" + encodeURIComponent(file);
      if (bl.getAttribute("src") !== src) bl.src = src;
      bl.style.height = (CFG.tower_logo_h > 0 ? CFG.tower_logo_h : 34) + "px";
    } else {
      bl.removeAttribute("src");
    }
  }
}

/* ── Regie (/regie steuert manuelle Panels, kommt im Payload mit) ─────────────────
   Anders als in test.html schaltet das hier NICHT direkt das Chart — die Charts-Seite
   liest REGIE.chart in ihrem eigenen Hook. So braucht core.js kein Chart-DOM. */
const REGIE = { chart: null, champ: false, battles: true, hotlap: true };
let lastRegie = { chart: "none" };
function applyRegie(rg) {
  rg = rg || {};
  lastRegie = rg;
  REGIE.chart   = (rg.chart && rg.chart !== "none") ? rg.chart : null;
  REGIE.champ   = !!rg.champ;
  REGIE.battles = rg.battles !== false;   // Default an
  REGIE.hotlap  = rg.hotlap  !== false;   // Default an
}
function regiePost(patch) {
  fetch("/api/regie", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(patch) }).catch(() => {});
}

/* ── deriveShared: alles, was sonst die Tower-Schleife nebenbei erledigt ──────── */
function deriveShared(data) {
  const sessionData = data.session || {};

  // Sektor-Bestzeiten + Gap-Historie + Bestrunden-Sektoren bei Session-Wechsel zuruecksetzen
  if (sessionData.type !== lastSecType) {
    lastSecType = sessionData.type;
    secBest = [Infinity, Infinity, Infinity];
    for (const k in drvSecBest) delete drvSecBest[k];
    for (const k in bestLapSecs) delete bestLapSecs[k];
    for (const k in prevLastLap) delete prevLastLap[k];
    for (const k in lastQualiSecs) delete lastQualiSecs[k];
    for (const k in curS3) delete curS3[k];
    for (const k in stintLen) delete stintLen[k];
    for (const k in prevComp) delete prevComp[k];
    for (const k in prevAge) delete prevAge[k];
    gapHist = {}; lapHist = {}; gapLastLap = 0;
    scRestartUntilLap = 0; scOnboardActive = false;
  }

  // Safety-Car-Statuswechsel -> Onboard-Fenster (die Rennleitungs-Meldung dazu macht
  // racemsg.js ueber KERS.onSc, damit core kein Meldungs-DOM braucht).
  const scStatus = data.connected ? sessionData.safety_car_status : "none";
  if (data.connected && scStatus !== lastScStatus) {
    if (scStatus === "sc" || scStatus === "vsc") {
      if (!sessionData.is_quali) scOnboardActive = true;   // Onboard-Fenster auf
    } else if (scStatus === "none" && (lastScStatus === "sc" || lastScStatus === "vsc")) {
      // Restart: Onboard auf P1 schalten, bis der Fuehrende Start/Ziel ueberquert
      const p1 = (data.drivers || []).find(x => x.position === 1);
      if (p1) scRestartUntilLap = (p1.lap_num || 0) + 1;
    }
    const prev = lastScStatus;
    lastScStatus = scStatus;
    scHooks.forEach(fn => { try { fn(scStatus, prev, data); } catch (e) { console.error("[KERS sc]", e); } });
  }

  // Nach Position sortieren (wie im Tower), damit alle Bausteine dieselbe Reihenfolge sehen.
  let drivers = (data.drivers || []).slice()
      .sort((a, b) => ((a.position || 999) - (b.position || 999)) || ((a.index || 0) - (b.index || 0)));
  if (CFG.rows > 0) drivers = drivers.slice(0, CFG.rows);   // nur Top-N (Settings/URL)

  // Gap-/Rundenzeiten-Verlauf mitschreiben: pro neuer Runde ein Schnappschuss
  if (!sessionData.is_quali && data.connected) {
    const gl = sessionData.current_lap || 0;
    if (gl > 0 && gl !== gapLastLap) {
      gapLastLap = gl;
      const snap = {}, lapSnap = {};
      drivers.forEach(d => { if (!d.dnf && !d.dsq) {
        snap[d.index] = Math.round((d.gap_to_leader || 0) * 100) / 100;
        if (d.last_lap > 0) lapSnap[d.index] = Math.round(d.last_lap * 1000) / 1000;
      } });
      gapHist[gl] = snap;
      lapHist[gl] = lapSnap;
    }
  }

  // Pro Fahrer: Bestsektoren fuehren (Quali S3-Sonderfall + Rennen)
  drivers.forEach(d => {
    mergeBestSectors(d);   // Bestsektoren vom Spiel uebernehmen

    const s3live = (d.last_lap > 0 && d.sector1 > 0 && d.sector2 > 0)
      ? d.last_lap - d.sector1 - d.sector2 : 0;

    // Rundenende erkennen: beim Ueberfahren der Start/Ziel-Linie setzt das Spiel sector1/
    // sector2 der NEUEN Runde schon auf 0 -> fuer S3 der GERADE beendeten Runde die zuletzt
    // gesehenen S1/S2 nehmen. S3-Bestsektor + Bestrunden-Sektoren daraus mitfuehren.
    if (sessionData.is_quali && d.last_lap > 0 && prevLastLap[d.index] !== d.last_lap) {
      prevLastLap[d.index] = d.last_lap;
      const ls = lastQualiSecs[d.index];
      const s1c = d.sector1 > 0 ? d.sector1 : (ls ? ls[0] : 0);
      const s2c = d.sector2 > 0 ? d.sector2 : (ls ? ls[1] : 0);
      const s3c = (s1c > 0 && s2c > 0) ? d.last_lap - s1c - s2c : 0;
      if (s3c > 0) {
        curS3[d.index] = s3c;                         // I34: S3 der beendeten Runde fuers Onboard
        const pb = drvSecBest[d.index] || (drvSecBest[d.index] = [Infinity, Infinity, Infinity]);
        if (s3c < pb[2]) pb[2] = s3c;                 // Bestsektor S3 mitfuehren
        if (s3c < secBest[2]) secBest[2] = s3c;       // Session-Best S3
        if (Math.abs(d.last_lap - (d.best_lap || 0)) < 0.005)
          bestLapSecs[d.index] = [s1c, s2c, s3c];     // Sektoren der Bestrunde einfrieren
      }
    }
    // Zuletzt gueltige S1/S2 der laufenden Runde merken -> ueberlebt den S/Z-Reset fuer S3 oben.
    if (d.sector1 > 0 && d.sector2 > 0) lastQualiSecs[d.index] = [d.sector1, d.sector2];

    if (sessionData.is_quali) {
      // Wie im Tower: nur auf einem echten Hotlap fliessen die LIVE-Sektoren in die Bests.
      if (qualiStatus(d) === "track") absorbSectors(d.index, [d.sector1, d.sector2, s3live]);
    } else {
      updateRaceBest(d.index, [d.sector1, d.sector2, s3live]);   // Basis fuer den Onboard-Delta-Balken
    }
  });

  if (!sessionData.is_quali && data.connected) trackStints(drivers);

  lastLapCount = sessionData.current_lap || lastLapCount;
  lastDrivers = drivers;
  return drivers;
}

/* ── Registry: jede Bausteinseite haengt sich mit KERS.onData(fn) ein ─────────── */
const hooks = [];
const scHooks = [];
window.KERS = {
  onData(fn) { hooks.push(fn); },
  onSc(fn) { scHooks.push(fn); },      // (status, prevStatus, data) bei SC-Wechsel
  cfg: CFG,
  regie: REGIE,
  /** true, wenn der Baustein angezeigt werden soll (auf Einzelseiten immer, ausser ?respect=1) */
  on(key) { return PART_FORCE_ON ? true : !!CFG[key]; },
};

let lastPayload = null;
function coreRender(data) {
  applySettings(data.settings);   // Server-Settings live uebernehmen (URL-Parameter gewinnen)
  applyBrand();
  data = maybeHoldQuali(data);    // ggf. eingefrorenes Quali-Ergebnis statt Live-Daten
  applyRegie(data.regie);
  const drivers = deriveShared(data);
  lastPayload = data;
  for (const fn of hooks) {
    try { fn(data, drivers); } catch (e) { console.error("[KERS part]", e); }
  }
}

/* ── Datenanbindung: SSE (Push) mit Polling-Fallback ──────────────────────────── */
let usingSSE = false, pollTimer = null;
async function fetchAndRender() {
  try {
    const res = await fetch("/api/live");
    coreRender(await res.json());
  } catch (e) { console.error("[KERS] fetch error:", e); }
}
function startPolling() {
  if (pollTimer) return;
  fetchAndRender();
  pollTimer = setInterval(fetchAndRender, 200);
}
function startSSE() {
  try {
    const es = new EventSource("/api/stream");
    es.onmessage = ev => { try { coreRender(JSON.parse(ev.data)); usingSSE = true; } catch (e) {} };
    es.onerror = () => {
      // Verbindung weg -> auf Polling zurueckfallen (EventSource versucht selbst reconnect,
      // aber wir wollen sicher Daten sehen)
      if (!usingSSE) { es.close(); startPolling(); }
    };
  } catch (e) { startPolling(); }
}

// Erst starten, wenn die Bausteinseite ihre Hooks registriert hat.
window.addEventListener("DOMContentLoaded", () => {
  if (QP.get("pad") === "0") document.body.classList.add("nopad");
  startSSE();
  setTimeout(() => { if (!usingSSE) startPolling(); }, 1500);   // SSE kam nicht -> Polling
});
