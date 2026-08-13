/* ═══════════════════════════════════════════════════════════════════════════════
   Leaderboard-Tower — aus templates/test.html extrahiert.

   Der Tower IST der grosse Teil von render() (test.html Z. 1787–2326). Alles, was
   dort NICHT zum Tower gehoert, ist hier entfernt und steckt in den anderen
   Bausteinseiten bzw. in core.js:

     entfernt -> core.js        applySettings/applyBrand/maybeHoldQuali, Sortierung,
                                Session-Wechsel-Reset, Gap-/Rundenzeiten-Historie,
                                Safety-Car-Zustand, Sektor-Buchhaltung (deriveShared)
     entfernt -> battles.js     updateBattlePrediction, updateBattleBox
     entfernt -> hotlap.js      updateHotlapBoxes
     entfernt -> onboard.js     updateOnboard
     entfernt -> trackmap.js    updateTrackmap
     entfernt -> lowerthird.js  updateLowerThird (+ trackStints -> core)
     entfernt -> pit.js         startPitStop/finishPitStop (Boxen-Erkennung)
     entfernt -> racemsg.js     race_control-Meldungen, Safety-Car-Meldung
     entfernt -> lights/flbanner/danger/champ/pitproj/undercut/charts

   Geblieben ist: Kopfzeile, Wetter-Ticker, Session-Titel, die Fahrer-Zeilen mit
   allem drum herum (Sektoren, Strafen, Reifen, Damage, Comeback, PB-Flash,
   Zielflagge, MOM/DRS), Elimination-Linie, SC-/VSC-/Rotflaggen-Rahmen und die
   automatische Tower-Skalierung.

   ⚠ Der Wetter-Ticker sitzt im .leaderboard-header und ist deshalb Teil dieser
     Seite — nicht separat platzierbar.
   ═══════════════════════════════════════════════════════════════════════════════ */

const ROW_HEIGHT = 62;
let renderFrame = 0;         // zaehlt Durchlaeufe -> erkennt Zeilen, die keiner mehr anfasst
const rowMiss = {};          // idx -> wie viele Frames am Stueck fehlt der Fahrer im Payload
const MISS_TOLERANCE = 3;    // ~0,25 s Aussetzer tolerieren (Paketverlust), dann Zeile weg
const FULL_GRID  = 22;       // feste Feldgroesse -> Tower IMMER gleich hoch (Q & R)
let towerMaxRows = 0;        // Sicherheitsnetz, falls doch mal >FULL_GRID Autos im Feld sind
const prevBestLap = {};      // letzte gesehene Bestrunde je Fahrer (fuer den Bestrunde-Flash)
let prevStandings = [];
let lastConnTs = 0, everConn = false;   // Ergebnis stehen lassen (CFG.holds)
let tickerIndex = 0, tickerItems = [], tickerShown = "";
let lastSession = null;
let redFlagUntil = 0;        // rote Flagge: Glow bis zu diesem Zeitstempel
let rfLastRcId = 0;          // eigene race_control-Sichtung, nur fuer das Rotflaggen-Event

const container      = document.getElementById("standings-body");
const outerContainer = document.querySelector(".leaderboard-container");


/* ══════ Tower-Skalierung (Schaerfe): zoom statt transform   (test.html Z. 1467–1507) ══════ */

// ── Tower-Skalierung: SCHÄRFE ────────────────────────────────────────────────
// `transform: scale()` rastert das Panel in Originalgröße und skaliert dann das fertige
// BILD -> bei 22 Fahrern (Faktor ~0.56) wird jeder Buchstabe interpoliert = matschig.
// `zoom` skaliert stattdessen das LAYOUT: der Text wird in Zielgröße neu gerendert.
//
// ⚠ Fallstrick (kostete den ersten Versuch): die Höhenmessung darf NICHT vom aktuell
// gesetzten Zoom abhängen, sonst schaukelt sich der Faktor Frame für Frame hoch, die
// CSS-Transitions der Zeilen werden dauernd neu gestartet und die Zeilen bleiben an
// Zwischenpositionen hängen. Deshalb: für die Messung den Zoom kurz abschalten und
// das Ergebnis cachen (die Rohhöhe ändert sich nur bei Header-/Feldänderungen).
// Notaus: ?sharp=0 in der URL erzwingt das alte transform-Verhalten.
const USE_ZOOM = (new URLSearchParams(location.search).get("sharp") !== "0")
                 && !!(window.CSS && CSS.supports && CSS.supports("zoom", "0.5"));
const TOWER_BASE = 1.12;          // Grundvergrößerung (war vorher CSS transform: scale)

function setTowerScale(el, f) {
  const v = (f > 0 ? f : 1);
  if (USE_ZOOM) {
    el.style.transform = "";
    el.style.zoom = v === 1 ? "" : v.toFixed(4);
  } else {
    el.style.zoom = "";
    el.style.transform = v === 1 ? "" : `scale(${v.toFixed(4)})`;
  }
}

/** Rohhöhe des Panels (ohne jede Skalierung), gecacht. */
let _rawH = 0, _rawKey = "", _rawAt = 0;
function towerRawHeight(el, key) {
  const now = performance.now();
  if (key !== _rawKey || now - _rawAt > 1000) {      // nur bei Änderung / 1x pro Sekunde
    const prevZoom = el.style.zoom, prevTf = el.style.transform;
    el.style.zoom = ""; el.style.transform = "";     // unskaliert messen
    _rawH = el.offsetHeight;
    el.style.zoom = prevZoom; el.style.transform = prevTf;
    _rawKey = key; _rawAt = now;
  }
  return _rawH;
}

// ── Ticker ────────────────────────────────────────────────────────────────────

/* ══════ Wetter-Ticker (sitzt im Kopf des Towers)   (test.html Z. 1508–1543) ══════ */

function buildTickerItems(session) {
  const items = [];
  if (session.weather_name) {
    const nowEmoji = session.weather_emoji || "";
    const nowRain  = session.weather_rain != null ? session.weather_rain : 0;
    let html = `<span class="fc-chip fc-now"><span class="fc-t">Jetzt</span>`
      + `<span class="fc-ico">${nowEmoji}</span><span class="fc-rain">${nowRain}%</span></span>`;
    const fc = (session.forecast || []).slice(0, 4);   // Jetzt + 4 = insgesamt 5 Chips
    html += fc.map(f =>
      `<span class="fc-chip"><span class="fc-t">+${f.t} Min</span>`
      + `<span class="fc-ico">${f.emoji}</span><span class="fc-rain">${f.rain}%</span></span>`
    ).join("");
    items.push(html);
  }
  // Nur noch Wetter im Ticker (kein Fastest-Lap-Wechsel mehr).
  return items.length ? items : [`<span class="tk-now">Race Telemetry</span>`];
}

function rotateTicker(session) {
  tickerItems = buildTickerItems(session);
  tickerIndex = (tickerIndex + 1) % tickerItems.length;
  const nextHTML = tickerItems[tickerIndex];
  // Gleicher Inhalt (z.B. in der Quali nur Wetter, unverändert) -> NICHT aufblinken.
  // Nur neu einblenden, wenn sich der Text wirklich ändert (z.B. +10 Min -> +5 Min).
  if (nextHTML === tickerShown) return;
  const el = document.getElementById("ticker-text");
  el.style.opacity = "0";
  setTimeout(() => {
    el.innerHTML = nextHTML;
    tickerShown = nextHTML;
    el.style.opacity = "1";
  }, 500);
}

// Rotate every 5 seconds
setInterval(() => { if (lastSession) rotateTicker(lastSession); }, 5000);

/* ══════ Sektor-Einfaerbung   (test.html Z. 1574–1603) ══════ */

function colorSectors(row, idx, secs) {
  const cls = ["s1", "s2", "s3"];
  const pb = drvSecBest[idx] || (drvSecBest[idx] = [Infinity, Infinity, Infinity]);
  secs.forEach((v, i) => {
    const span = row.querySelector("." + cls[i]);
    span.classList.remove("sp", "sg", "sy");
    if (!(v > 0)) return;
    if (v <= secBest[i] + 0.005) span.classList.add("sp");        // Session-Best
    else if (v <= pb[i] + 0.005) span.classList.add("sg");        // persönlich
    else span.classList.add("sy");                                // langsamer
    if (v < secBest[i]) secBest[i] = v;
    if (v < pb[i]) pb[i] = v;
  });
}
function clearSectorColors(row) { row.querySelectorAll(".col-sectors span").forEach(s => s.classList.remove("sp", "sg", "sy")); }
// Box/Outlap/INLAP: die 3 Sektoren der SCHNELLSTEN RUNDE des Fahrers anzeigen
// (eine echte, zusammenhängende Runde — nicht zusammengesetzte Bestsektoren).
// Fallback auf Bestsektoren, solange noch keine komplette Bestrunde erfasst ist.
// Lila = Session-Best, sonst grün.
function showBestSectors(row, idx) {
  const cls = ["s1", "s2", "s3"];
  const src = bestLapSecs[idx] || drvSecBest[idx] || [Infinity, Infinity, Infinity];
  src.forEach((v, i) => {
    const span = row.querySelector("." + cls[i]);
    span.classList.remove("sp", "sg", "sy");
    if (!(v > 0 && v < Infinity)) { span.textContent = "—"; return; }
    span.textContent = fmtTime(v);
    span.classList.add(v <= secBest[i] + 0.005 ? "sp" : "sg");
  });
}

/* ══════ Damage-Icons   (test.html Z. 2673–2701) ══════ */

// ── Damage-Icons ────────────────────────────────────────────────────────────
// Front-/Heckflügelschaden als kleine Icons. Schwellen: >30% = gelb, >60% = rot (blinkt).
const DMG_MIN = 15;              // darunter gilt als unbeschädigt (kein Farb-Highlight)
const DMG_NEUTRAL = "#3a3a44";   // unbeschädigtes Teil (dunkel)
// Schaden-% -> Farbe: gelb (leicht) über orange nach rot (schwer).
function dmgColor(pct) {
  if (pct < DMG_MIN) return DMG_NEUTRAL;
  const f = Math.max(0, Math.min(1, (pct - 25) / 60));   // 25%..85% -> gelb..rot
  return `rgb(255,${Math.round(208 - 170 * f)},${Math.round(24 * (1 - f))})`;
}
// Flügel-Icon aus dem Bild (per CSS-Maske) in der Schadensfarbe.
function wingSpan(kind, label, pct) {
  const crit = pct > (CFG.dmgcrit || 60) ? " crit" : "";
  return `<span class="dmg-wing ${kind}${crit}" style="background-color:${dmgColor(pct)}" `
    + `title="${label} ${pct}%"></span>`;
}
function renderDamage(el, d, isQuali) {
  // Ausgeschiedene (DNF/DSQ) zeigen keinen Schaden mehr -> kein ewiges Weiterblinken.
  if (!CFG.damage || isQuali || d.dnf || d.dsq) { if (el.childElementCount) { el.innerHTML = ""; el.dataset.sig = ""; } return; }
  const fl = d.dmg_fl || 0, fr = d.dmg_fr || 0, rw = d.dmg_rw || 0;
  const sig = fl + "|" + fr + "|" + rw;
  if (el.dataset.sig === sig) return;             // nur bei Änderung neu bauen
  el.dataset.sig = sig;
  let html = "";
  if (fl >= DMG_MIN) html += wingSpan("dw-fwl", "Frontflügel links", fl);
  if (fr >= DMG_MIN) html += wingSpan("dw-fwr", "Frontflügel rechts", fr);
  if (rw >= DMG_MIN) html += wingSpan("dw-rw", "Heckflügel", rw);
  el.innerHTML = html;
}


/* ══════ Der Tower-Teil von render()   (test.html Z. 1791–2326, gefiltert) ══════ */
function renderTower(data, drivers) {
  const sessionData = data.session || {};

  const indicator = document.getElementById("liveIndicator");
  const liveText  = document.getElementById("liveText");
  if (data.connected) {
    indicator.className = "live-indicator";
    if (liveText) liveText.textContent = "LIVE";
  } else {
    indicator.className = "live-indicator waiting";
    if (liveText) liveText.textContent = "WAITING";
  }
  // F1 26: statt DRS gibt es den "Overtake Mode" (eigenes UDP-Paket) -> Label "MOM".
  const isF126 = sessionData.formula === 13 || /26/.test(sessionData.formula_name || "");
  const boostLabel = isF126 ? "MOM" : "DRS";
  const drsHeader = document.getElementById("drs-header");
  const hdrLeader = document.getElementById("hdr-leader");
  const hdrInterval = document.getElementById("hdr-interval");
  if (sessionData.is_quali) {                 // Quali-Spalten: BEST · GAP · STATUS
    if (hdrLeader) hdrLeader.textContent = "BEST";
    if (hdrInterval) hdrInterval.textContent = "GAP";
    if (drsHeader) drsHeader.textContent = "STATUS";
  } else {
    if (hdrLeader) hdrLeader.textContent = "LEADER";
    if (hdrInterval) hdrInterval.textContent = "INTERVAL";
    if (drsHeader) drsHeader.textContent = boostLabel;
  }
  // Session title
  const titleEl = document.getElementById("sessionTitle");
  const subEl = document.getElementById("sessionSub");

  if (sessionData.is_quali) {
    // Quali: "Q1" im Titel, Zeit im Sub
    if (sessionData.type_name && sessionData.type_name !== "Unbekannt") {
      titleEl.textContent = sessionData.type_name.toUpperCase();
    }
    if (sessionData.time_left > 0) {
      const m = Math.floor(sessionData.time_left / 60);
      const s = Math.floor(sessionData.time_left % 60).toString().padStart(2, "0");
      subEl.textContent = `${m}:${s}`;
    } else {
      subEl.textContent = "";
    }
  } else {
    // Rennen: nur "Runde X / Y" — kein "RENNEN"
    titleEl.textContent = "";
    if (sessionData.total_laps > 0) {
      subEl.textContent = `Runde ${sessionData.current_lap} / ${sessionData.total_laps}`;
    } else {
      subEl.textContent = "";
    }
  }

  if (!lastSession) {
    lastSession = sessionData;
    const el = document.getElementById("ticker-text");
    const items = buildTickerItems(sessionData);
    el.innerHTML = items[0] || "—";
    tickerShown = items[0] || "—";
    el.style.opacity = "1";
  } else {
    lastSession = sessionData;
  }

  // Sectors visible only in quali
  const container = document.getElementById("standings-body");
  const outerContainer = document.querySelector(".leaderboard-container");
  // Sichtbarkeit aus den Settings: Tower + Wetter-Ticker
  outerContainer.style.display = KERS.on("tower") ? "" : "none";
  const tickerWrap = document.querySelector(".ticker-wrap");
  if (tickerWrap) tickerWrap.style.display = CFG.ticker ? "" : "none";
  if (sessionData.is_quali) {
    outerContainer.classList.remove("sectors-hidden");
    outerContainer.classList.add("quali-mode");
  } else {
    outerContainer.classList.add("sectors-hidden");
    outerContainer.classList.remove("quali-mode");
  }

  // "Warte auf Telemetrie" nur zeigen, wenn NOCH NIE Daten kamen ODER das letzte
  // Ergebnis länger als CFG.holds her ist. Direkt nach einer Session bleiben die
  // letzten Standings so 5 Min stehen (kein Ergebnis-Popup mehr nötig).
  if (data.connected) { lastConnTs = performance.now(); everConn = true; }
  const holdResult = everConn && !data.connected && (performance.now() - lastConnTs) / 1000 < (CFG.holds || 300);
  outerContainer.classList.toggle("disconnected", !data.connected && !holdResult);

  // Safety Car / Virtual Safety Car Glow-Rahmen
  const scStatus = data.connected ? sessionData.safety_car_status : "none";
  outerContainer.classList.toggle("sc-active",  scStatus === "sc");
  outerContainer.classList.toggle("vsc-active", scStatus === "vsc");
  // "Safety Car" / "Letzte Runde" als kompaktes Badge direkt neben Runde X/Y
  const finalLap = !sessionData.is_quali && sessionData.total_laps > 0 && sessionData.current_lap >= sessionData.total_laps;
  const flag = document.getElementById("sessionFlag");
  if (flag) {
    if (sessionData._quali_result) { flag.className = "session-flag fin"; flag.textContent = "🏁 Ergebnis"; }
    else if (scStatus === "sc")       { flag.className = "session-flag sc";  flag.textContent = "Safety Car"; }
    else if (scStatus === "vsc") { flag.className = "session-flag sc";  flag.textContent = "Virtual SC"; }
    else if (data.connected && finalLap) { flag.className = "session-flag fin"; flag.textContent = "🏁 Letzte Runde"; }
    else { flag.className = "session-flag"; flag.textContent = ""; }
  }

  // (Safety-Car-MELDUNG -> racemsg.js; der Zustand kommt aus core.js)

  const body    = container;
  const tpl     = document.getElementById("row-tpl");

  // #2: Tower immer gleich groß — Höhe fürs VOLLE Feld (FULL_GRID) reservieren, nicht nach der
  // aktuellen Fahrerzahl -> schrumpft nie; fehlende/ausgefallene Fahrer lassen unten Platz frei.
  if (drivers.length > towerMaxRows) towerMaxRows = drivers.length;
  const towerRows = CFG.rows > 0 ? CFG.rows : Math.max(FULL_GRID, towerMaxRows);
  // Panel nur so hoch wie tatsächlich Fahrer da sind -> kein graues Leerfeld unten.
  // Die GLEICHE Größe (Zeilenhöhe) kommt aus der Skalierung weiter unten (Referenz = towerRows).
  // Platz = Array-Index. Das ist sicher, WEIL drivers oben nach position sortiert wird:
  // die Rangfolge stimmt dadurch, und der Index liegt immer in [0, Fahreranzahl-1] —
  // eine Zeile kann also nie aus dem Panel geschoben werden.
  // (Ein Versuch, stattdessen die Position selbst zu nehmen (position-1), hat genau das
  // verursacht: bei einer Position, die nicht zum aktuellen Feld passte, landete die
  // Zeile weit unterhalb des Panels.)
  body.style.height = drivers.length ? `${drivers.length * ROW_HEIGHT}px` : "0px";

  // Quali: Pole-Zeit (schnellste Bestrunde) + Elimination-Schnitt je Q-Segment
  let poleTime = 0, elimCut = 0;
  if (sessionData.is_quali) {
    const bls = drivers.map(d => d.best_lap || 0).filter(x => x > 0);
    poleTime = bls.length ? Math.min(...bls) : 0;
    if (sessionData.type === 5) elimCut = 16;        // Q1 -> P17–P22 raus
    else if (sessionData.type === 6) elimCut = 10;   // Q2 -> P11–P16 raus
  }

  // Index -> Zeile EINMAL pro Frame mappen (statt O(n²) querySelectorAll je Fahrer)
  // -> deutlich weniger Main-Thread-Arbeit = flüssigere Animationen.
  const rowByIdx = {};
  body.querySelectorAll(".driver-row").forEach(r => { rowByIdx[r.dataset.idx] = r; });
  const frameTag = String(++renderFrame);   // Stempel, um Waisen-Zeilen zu erkennen

  // Fahrer, die nicht mehr im Feld sind, sanft ausblenden statt hart entfernen.
  // WICHTIG: kurze Aussetzer im Payload NICHT sofort als "weg" werten. Ein einziger
  // unvollständiger Payload reichte sonst aus, damit die Zeile 450 ms lang halbtransparent
  // an ihrer ALTEN Stelle stehen bleibt (genau die Geister über P19/P20 in den Screenshots).
  const liveKeys = new Set(drivers.map(d => String(d.index)));
  for (const key in rowByIdx) {
    if (liveKeys.has(key)) { delete rowMiss[key]; continue; }   // wieder da -> Zähler zurück
    rowMiss[key] = (rowMiss[key] || 0) + 1;
    // Toleranz aufgebraucht -> Zähler löschen, das Aufräumen unten entfernt die Zeile.
    if (rowMiss[key] >= MISS_TOLERANCE) delete rowMiss[key];
  }

  drivers.forEach((d, idx) => {
    mergeBestSectors(d);   // Bestsektoren vom Spiel übernehmen, bevor gefärbt wird
    // Eindeutiger Fahrer-Index als Key -> identische Namen können keine Zeile "stehlen"
    let row = rowByIdx[String(d.index)] || null;
    // (Früher gab es hier eine Sonderbehandlung für ausblendende Zeilen — es gibt keine
    //  mehr: Zeilen werden entfernt, sobald ihr Fahrer aus dem Payload fällt.)
    const isNew = !row;

    if (isNew) {
      const clone = tpl.content.cloneNode(true);
      row = clone.querySelector(".driver-row");
      if (!row) { console.error("Template clone failed!"); return; }
      row.dataset.idx = String(d.index);
      row.classList.add("entering");                               // startet unsichtbar
      // Neue Zeile OHNE Transform-Transition direkt an ihre Position setzen, damit sie
      // dort einblendet statt aus der Ecke heranzugleiten.
      row.style.transition = "none";
      row.style.transform = `translate3d(0,${idx * ROW_HEIGHT}px,0)`;
      row.dataset.y = String(idx * ROW_HEIGHT);
      body.appendChild(row);
      void row.offsetWidth;                                        // Reflow -> Position "festnageln"
      row.style.transition = "";                                   // CSS-Transition wieder aktiv
      // Im nächsten Frame einblenden -> sanftes Fade-In statt Aufploppen.
      requestAnimationFrame(() => requestAnimationFrame(() => row.classList.remove("entering")));
    }
    row.dataset.frame = frameTag;   // in diesem Frame angefasst (Waisen-Aufräumen unten)
    // Name kann sich ändern (Participants-Paket kommt nach Lap-Daten) -> immer setzen
    row.querySelector(".driver-name").textContent = d.name;

    const color = TEAM_COLORS[d.team] || "#fff";
    row.querySelector(".team-color-strip").style.backgroundColor = color;
    row.querySelector(".driver-team-sub").textContent = d.team || "";
    setTeamLogo(row.querySelector(".team-logo"), d.team);
    // Damage-Icons: Frontflügel (🛞→ Front) und Heckflügel. Nur im Rennen, ab Schwelle.
    renderDamage(row.querySelector(".dmg-icons"), d, sessionData.is_quali);
    // Strafen/Track-Limits NUR im Rennen (in der Quali gibt es keine) -> sonst ausblenden
    const penEl = row.querySelector(".pen-badge");
    const tlEl  = row.querySelector(".tl-badge");
    if (sessionData.is_quali) {
      if (penEl) { penEl.className = "pen-badge"; penEl.textContent = ""; }
      if (tlEl)  { tlEl.className  = "tl-badge";  tlEl.textContent  = ""; }
    } else {
      // Strafe-Pille: Durchfahrtstrafe (orange) hat Vorrang vor der Zeitstrafe (rot, +Xs)
      if (penEl) {
        penEl.classList.remove("dt");
        if ((d.pen_dt || 0) > 0 && !d.dsq) {
          penEl.textContent = "DT"; penEl.classList.add("dt", "show");
        } else if ((d.penalties || 0) > 0 && !d.dsq) {
          penEl.textContent = "+" + d.penalties + "s"; penEl.classList.add("show");
        } else {
          penEl.classList.remove("show"); penEl.textContent = "";
        }
      }
      // Track-Limits: 1. Verwarnung gelb, 2. orange; bei der 3. gibt es die Strafe
      // (erscheint dann als +Xs) und der Zähler beginnt von vorn -> %3, "immer wieder".
      if (tlEl) {
        const strike = (d.corner_warnings || 0) % 3;   // Verwarnungen im aktuellen Zyklus (3 = Strafe, Reset)
        tlEl.classList.remove("warn1", "warn2");
        if ((strike === 1 || strike === 2) && !d.dsq) {
          tlEl.textContent = String(strike);            // nur die Zahl der Verwarnungen statt "TL"
          tlEl.classList.add(strike === 1 ? "warn1" : "warn2", "show");
        } else { tlEl.classList.remove("show"); tlEl.textContent = ""; }
      }
    }

    row.querySelector(".rank").textContent = d.position;
    const isFl = sessionData.fastest_lap_driver && d.name === sessionData.fastest_lap_driver;
    // Nur die Zustands-Klassen togglen statt className zu überschreiben -> entering/leaving
    // (Fade-In/-Out) bleiben erhalten.
    row.classList.add("leaderboard-row", "driver-row");
    row.classList.toggle("rank-1", d.position === 1);
    row.classList.toggle("rank-2", d.position === 2);   // Silber
    row.classList.toggle("rank-3", d.position === 3);   // Bronze
    row.classList.toggle("fl-lap", isFl);
    row.classList.toggle("elim-zone", elimCut > 0 && d.position > elimCut);   // Quali: Aus-Zone
    row.classList.toggle("is-finished", !!d.finished);
    // Unterste Zeile rundet die unteren Panel-Ecken ab (siehe .last-row in tower.css).
    row.classList.toggle("last-row", idx === drivers.length - 1);   // #4: Zielflagge (beendet, Rennen & Quali)
    row.dataset.even = idx % 2 === 0 ? "1" : "0";
    row.classList.toggle("focused", data.focus_index != null && data.focus_index >= 0 && d.index === data.focus_index);

    const prevIdx = prevStandings.findIndex(p => p.index === d.index);
    const ind = row.querySelector(".change-indicator");
    if (!isNew && prevIdx !== -1 && prevIdx !== idx) {
      const gained = prevIdx > idx;   // kleinerer Index = weiter vorne = Platz gewonnen
      ind.innerHTML = gained ? "▲" : "▼";
      ind.className = `change-indicator ${gained ? "change-up" : "change-down"}`;
      row.dataset.lastMove = Date.now();
      // Positions-Flash: Nummer kurz grün (gewonnen) / rot (verloren) aufleuchten.
      const posEl = row.querySelector(".col-pos");
      posEl.classList.remove("pos-flash-up", "pos-flash-down");
      void posEl.offsetWidth;         // Reflow erzwingen -> Animation startet neu
      posEl.classList.add(gained ? "pos-flash-up" : "pos-flash-down");
    } else if (Date.now() - parseInt(row.dataset.lastMove || 0) > 5000) {
      ind.innerHTML = "-";
      ind.className = "change-indicator change-none";
    }

    // R6: Comeback seit dem Start (Startplatz vs. aktuelle Position), nur im Rennen.
    const cbEl = row.querySelector(".comeback-badge");
    if (cbEl) {
      const gp = d.grid_position || 0;
      const cbDelta = (!sessionData.is_quali && gp > 0 && !d.dnf && !d.dsq) ? gp - d.position : 0;
      if (CFG.comeback && cbDelta !== 0) {
        cbEl.textContent = (cbDelta > 0 ? "▲" : "▼") + Math.abs(cbDelta);
        cbEl.className = "comeback-badge show " + (cbDelta > 0 ? "cb-up" : "cb-down");
      } else { cbEl.className = "comeback-badge"; cbEl.textContent = ""; }
    }

    // R7: Bestrunde-Flash - neue persönliche Bestzeit blitzt grün, neue Session-Bestzeit lila.
    const bl2 = d.best_lap || 0, prevBl = prevBestLap[d.index];
    if (CFG.pbflash && !isNew && bl2 > 0 && prevBl && bl2 < prevBl - 0.0005) {
      const purple = sessionData.fastest_lap_driver && d.name === sessionData.fastest_lap_driver;
      row.classList.remove("pb-flash-g", "pb-flash-p");
      void row.offsetWidth;                              // Reflow -> Animation startet neu
      row.classList.add(purple ? "pb-flash-p" : "pb-flash-g");
      clearTimeout(row._pbT);
      row._pbT = setTimeout(() => row.classList.remove("pb-flash-g", "pb-flash-p"), 1500);
    }
    if (bl2 > 0) prevBestLap[d.index] = bl2;

    // Smooth reorder: CSS-Transition interpoliert von der aktuellen Position,
    // auch wenn mitten in einer laufenden Bewegung ein neues Ziel kommt.
    const newY = idx * ROW_HEIGHT;
    row.dataset.y = String(newY);   // fürs Aufräumen unten: wo steht die Zeile gerade?
    row.style.transform = `translate3d(0,${newY}px,0)`;

    const gapEl = row.querySelector(".col-gap");
    const lapEl = row.querySelector(".col-lap");
    if (sessionData.is_quali) {
      // BEST = eigene Bestrunde, GAP = Rückstand auf Pole
      const bl = d.best_lap || 0;
      gapEl.className = "col-gap"; gapEl.style.display = "";
      gapEl.textContent = bl > 0 ? fmtTime(bl) : "—";
      lapEl.style.display = "";
      if (bl > 0 && poleTime > 0) {
        const gp = bl - poleTime;
        if (gp < 0.0005) { lapEl.textContent = "POLE"; lapEl.className = "col-lap leader"; }
        else { lapEl.textContent = "+" + gp.toFixed(3); lapEl.className = "col-lap"; }
      } else { lapEl.textContent = "—"; lapEl.className = "col-lap no-data"; }
    } else if (d.dsq) {
      gapEl.textContent = "DSQ"; gapEl.className = "col-gap dsq";
      lapEl.textContent = "—";   lapEl.className = "col-lap no-data"; lapEl.style.display = "";
    } else if (d.dnf) {
      gapEl.textContent = "DNF"; gapEl.className = "col-gap dnf";
      lapEl.textContent = "—";   lapEl.className = "col-lap no-data"; lapEl.style.display = "";
    } else if (d.position === 1) {
      // Führender: "LEADER"-Banner über beide Gap-Spalten
      gapEl.textContent = "LEADER"; gapEl.className = "col-gap leader leader-banner";
      lapEl.textContent = "";       lapEl.className = "col-lap"; lapEl.style.display = "none";
    } else if (d.laps_down >= 1) {
      // Überrundet: statt Zeitabstand die Runden-Differenz zum Führenden
      gapEl.textContent = "+" + d.laps_down + (d.laps_down === 1 ? " LAP" : " LAPS");
      gapEl.className = "col-gap lapped";
      lapEl.textContent = fmtGap(d.gap_to_ahead);  lapEl.className = "col-lap"; lapEl.style.display = "";
    } else {
      gapEl.textContent = fmtGap(d.gap_to_leader); gapEl.className = "col-gap";
      lapEl.textContent = fmtGap(d.gap_to_ahead);  lapEl.className = "col-lap"; lapEl.style.display = "";
    }

    setTyreIcon(row.querySelector(".tyre-icon"), d.compound);
    row.querySelector(".tyre-age").innerHTML = `Rnd <b>${d.tyre_age}</b>`;

    // (Boxen-Erkennung + Pit-Timer -> pit.js)


    const s3 = (d.last_lap > 0 && d.sector1 > 0 && d.sector2 > 0)
      ? d.last_lap - d.sector1 - d.sector2 : 0;
    // Rundenende erkennen: beim Überfahren der Start/Ziel-Linie setzt das Spiel sector1/
    // sector2 der NEUEN Runde schon auf 0 -> für S3 der GERADE beendeten Runde die zuletzt
    // gesehenen S1/S2 nehmen (sonst fehlt S3 auf Inlap/Box/Outlap – "teilweise", je nach
    // Paket-Timing). S3-Bestsektor + Bestrunden-Sektoren daraus mitführen.
    if (sessionData.is_quali && d.last_lap > 0 && prevLastLap[d.index] !== d.last_lap) {
      prevLastLap[d.index] = d.last_lap;
      const ls = lastQualiSecs[d.index];
      const s1c = d.sector1 > 0 ? d.sector1 : (ls ? ls[0] : 0);
      const s2c = d.sector2 > 0 ? d.sector2 : (ls ? ls[1] : 0);
      const s3c = (s1c > 0 && s2c > 0) ? d.last_lap - s1c - s2c : 0;
      if (s3c > 0) {
        curS3[d.index] = s3c;                         // I34: S3 der beendeten Runde fürs Onboard
        const pb = drvSecBest[d.index] || (drvSecBest[d.index] = [Infinity, Infinity, Infinity]);
        if (s3c < pb[2]) pb[2] = s3c;                 // Bestsektor S3 mitführen
        if (s3c < secBest[2]) secBest[2] = s3c;       // Session-Best S3
        if (Math.abs(d.last_lap - (d.best_lap || 0)) < 0.005)
          bestLapSecs[d.index] = [s1c, s2c, s3c];     // Sektoren der Bestrunde einfrieren
      }
    }
    // Zuletzt gültige S1/S2 der laufenden Runde merken -> überlebt den S/Z-Reset für S3 oben.
    if (d.sector1 > 0 && d.sector2 > 0) lastQualiSecs[d.index] = [d.sector1, d.sector2];
    // Quali-Status (Box/Outlap/Inlap/Hotlap) einmal bestimmen, für Sektoren UND Statuspille.
    const qualiSt = qualiStatus(d);
    if (sessionData.is_quali) {
      if (qualiSt === "track") {
        // Hotlap / on track -> aktuell gefahrene Sektoren (und Bestzeiten mitführen)
        row.querySelector(".s1").textContent = fmtTime(d.sector1) || "—";
        row.querySelector(".s2").textContent = fmtTime(d.sector2) || "—";
        row.querySelector(".s3").textContent = fmtTime(s3) || "—";
        colorSectors(row, d.index, [d.sector1, d.sector2, s3]);
      } else {
        // Box / Outlap -> schnellste (Best-)Sektoren des Fahrers
        showBestSectors(row, d.index);
      }
    } else {
      row.querySelector(".s1").textContent = fmtTime(d.sector1) || "—";
      row.querySelector(".s2").textContent = fmtTime(d.sector2) || "—";
      row.querySelector(".s3").textContent = fmtTime(s3) || "—";
      clearSectorColors(row);
      updateRaceBest(d.index, [d.sector1, d.sector2, s3]);   // Basis für den Onboard-Delta-Balken
    }
    // Q5: ungültige (Track-Limits-)Runde auf einem Hotlap -> Sektoren rot
    row.classList.toggle("lap-invalid", sessionData.is_quali && qualiSt === "track" && !!d.lap_invalid);

    const drsEl = row.querySelector(".col-drs");
    if (sessionData.is_quali) {
      drsEl.textContent = qualiSt === "track" ? "HOTLAP" : qualiSt === "inlap" ? "INLAP"
                        : qualiSt === "out" ? "OUTLAP" : "BOX";
      drsEl.className = "col-drs st st-" + qualiSt;
    } else if (d.dnf || d.dsq) {
      // Ausgeschieden: "PIT" nur wenn in der Box aufgegeben, sonst nichts anzeigen.
      drsEl.textContent = d.in_pit ? "PIT" : "";
      drsEl.className   = d.in_pit ? "col-drs in-pit-q" : "col-drs";
    } else if (d.in_pit) {
      drsEl.textContent = "PIT";
      drsEl.className   = "col-drs in-pit-q";
    } else {
      drsEl.textContent = boostLabel;
      if (isF126) {
        // 2026 Overtake Mode (Car-Telemetry-2-Paket): blau leuchtend wenn aktiv,
        // dezent blau wenn verfügbar (im 1s-Fenster), sonst aus.
        drsEl.className = d.overtake_active ? "col-drs boost-active"
                       : d.overtake_available ? "col-drs boost-ready"
                       : "col-drs";
      } else {
        drsEl.className = d.drs ? "col-drs active" : "col-drs";
      }
    }
  });
  // Sicherheitsnetz gegen "Geister-Zeilen": alles, was in diesem Durchlauf NICHT angefasst
  // wurde und auch nicht gerade ausblendet, ist eine Waise (z. B. ein Duplikat mit gleichem
  // data-idx, das im rowByIdx-Map überschrieben wurde -> würde nie wieder aktualisiert oder
  // entfernt und bliebe für immer an seiner alten Stelle stehen).
  // Harte Regel: im Tower steht NUR, was in diesem Durchlauf gerendert wurde.
  // Ausblendende Zeilen waren genau der Glitch: sie behalten ihre alte Y-Position,
  // während die anderen nachrücken — sie überlappen dann entweder mit der Zeile, die
  // ihren Platz übernommen hat, oder hängen (bei geschrumpftem Feld) unter dem Panel.
  // Einzige Ausnahme: Fahrer, die nur kurz aus dem Payload gefallen sind (Toleranz oben) —
  // die stehen ja noch an der richtigen Stelle und kommen gleich wieder.
  body.querySelectorAll(".driver-row").forEach(r => {
    if (r.dataset.frame === frameTag) return;      // in diesem Frame gerendert -> bleibt
    if (rowMiss[r.dataset.idx]) return;            // kurzer Aussetzer -> ein paar Frames warten
    r.remove();
  });

  // Rote Flagge: eigenes Sichten des race_control-Feeds (im Gesamt-Overlay faellt
  // redFlagUntil in der Meldungs-Schleife mit ab, die jetzt in racemsg.js steckt).
  const rc = data.race_control || [];
  if (rc.length && rc[rc.length - 1].id < rfLastRcId) rfLastRcId = 0;
  rc.forEach(m => {
    if (m.id <= rfLastRcId) return;
    rfLastRcId = m.id;
    if (m.type === "redflag") redFlagUntil = Date.now() + 15000;
  });
  outerContainer.classList.toggle("rf-active", data.connected && Date.now() < redFlagUntil);

  // Elimination-Linie positionieren
  const elimLine = document.getElementById("elim-line");
  if (elimLine) {
    if (elimCut > 0) { elimLine.style.display = "block"; elimLine.style.top = (elimCut * ROW_HEIGHT) + "px"; }
    else elimLine.style.display = "none";
  }

  // Tower auf die Canvas-Höhe einpassen, damit das komplette Feld (P1..letzter)
  // sichtbar bleibt und unten nichts abgeschnitten wird. Skaliert von oben-links,
  // bleibt also links verankert.
  outerContainer.style.transformOrigin = "top left";
  {
    const stageH   = window.innerHeight || 1080;
    // Skalierung IMMER fürs volle Feld (towerRows) rechnen -> Zeilen-Größe bleibt konstant
    // (Q & R gleich), egal wie viele Fahrer gerade da sind. Referenzhöhe = Panel ohne Body
    // (Header etc.) + towerRows Zeilen, statt der tatsächlichen (kürzeren) Body-Höhe.
    const rawH     = towerRawHeight(outerContainer, `${drivers.length}|${towerRows}|${stageH}`);
    const refH     = rawH - (drivers.length * ROW_HEIGHT) + (towerRows * ROW_HEIGHT);
    const availH   = (stageH - 40) * 0.80;                  // Tower nur ~80% der Höhe -> kompakter, nicht über den ganzen Screen
    // TOWER_BASE gehört zur Zielgröße -> mit einrechnen (kürzt sich raus: gleiche
    // Endgröße wie mit dem alten CSS-scale(1.12), nur eben scharf gerendert).
    const want     = refH > 0 ? availH / (refH * TOWER_BASE) : 1;
    const autoFit  = TOWER_BASE * Math.min(1, want);        // genau das, was "Auto" liefert

    // CFG.scale ist ein Faktor RELATIV zum eingepassten Wert, kein absoluter Zoom
    // (Änderung 04.08.2026, identisch in templates/test.html):
    //   0 (oder leer) = Auto        1.00 = exakt wie Auto
    //   1.20 = 20 % größer          0.80 = 20 % kleiner
    // ⚠ Auf DIESER Seite hängt der eingepasste Wert an der Höhe der OBS-Quelle, nicht
    //   an 1080p — eine kleine Quelle rechnet also anders als das Gesamt-Overlay.
    setTowerScale(outerContainer, autoFit * (CFG.scale > 0 ? CFG.scale : 1));
  }

  prevStandings = drivers;
}

KERS.onData((data, drivers) => renderTower(data, drivers));
