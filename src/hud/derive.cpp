#include "derive.h"

#include <QElapsedTimer>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace kers::hud {

// ── Konfiguration ────────────────────────────────────────────────────────────
const QVariantMap& Config::defaults()
{
    // 1:1 aus DEFAULT_CFG in core.js / derive.py. Der Server schickt seine
    // Settings mit jedem Payload mit; was er nicht kennt, bleibt auf der Vorgabe.
    static const QVariantMap d = {
        {"tower", true}, {"battles", true}, {"map", true}, {"onboard", true}, {"lights", true},
        {"damage", true}, {"msgs", true}, {"ticker", true}, {"lowerthird", true}, {"flbanner", true},
        {"undercut", true}, {"deltabar", true}, {"mapnumbers", true}, {"mapflags", true},
        {"pred", true}, {"danger", true}, {"comeback", true}, {"pbflash", true}, {"fresh", true},
        {"strat", true}, {"pitproj", true},
        {"brand_title", QString()}, {"brand_accent", QStringLiteral("#e10600")},
        {"header_color", QString()}, {"row_color", QString()},
        {"tower_logo", QString()}, {"tower_logo_pos", QStringLiteral("left")}, {"tower_logo_h", 34},
        {"scale", 0.0}, {"rows", 0}, {"mapsize", 400}, {"maprot", 110}, {"mapflip", true}, {"dotsize", 1.0},
        {"holds", 300}, {"ltdur", 4.0}, {"flbdur", 4.5}, {"dmgcrit", 60}, {"battlethresh", 1.5},
        {"preset", QStringLiteral("voll")},
        {"ampel", true},
        {"mapcorner", QStringLiteral("tr")},
        {"opacity", 1.0}, {"text_outline", 0.0},
        {"battleboxes", 4},
        {"hotlapboxes", 4},
        {"battledir", QStringLiteral("row")},
        {"penside", QStringLiteral("left")},
        {"penhidefinish", true},
        {"podiumstil", QStringLiteral("flat")},
        {"trackname", true},
        {"layout", QVariantMap()},
    };
    return d;
}

Config::Config(const QVariantMap& overrides)
    : m_override(overrides)
    , m_values(defaults())
{
}

void Config::apply(const QJsonObject& settings)
{
    const QVariantMap& def = defaults();
    for (auto it = def.cbegin(); it != def.cend(); ++it) {
        const QString& key = it.key();
        if (m_override.contains(key)) {
            m_values[key] = m_override.value(key);
            continue;
        }
        const QJsonValue v = settings.value(key);
        if (!v.isUndefined() && !v.isNull())
            m_values[key] = v.toVariant();
        else
            m_values[key] = it.value();
    }
}

void Config::setOverride(const QString& key, const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        m_override.remove(key);
    else
        m_override[key] = value;
}

QVariant Config::value(const QString& key) const
{
    const auto it = m_values.constFind(key);
    if (it != m_values.cend())
        return it.value();
    return defaults().value(key);
}

// ── Hilfsgroessen ────────────────────────────────────────────────────────────
double s3Live(const QJsonObject& d)
{
    const double last = jd(d, "last_lap");
    const double s1 = jd(d, "sector1");
    const double s2 = jd(d, "sector2");
    if (last > 0 && s1 > 0 && s2 > 0)
        return last - s1 - s2;
    return 0.0;
}

namespace {

double monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}

}  // namespace

// ── SharedState ──────────────────────────────────────────────────────────────
SharedState::SharedState(Config& cfg)
    : m_cfg(cfg)
    , m_clock(&monotonicMs)
{
}

std::array<double, 3>& SharedState::pb(int idx)
{
    auto it = m_drvSecBest.find(idx);
    if (it == m_drvSecBest.end())
        it = m_drvSecBest.insert(idx, {INF, INF, INF});
    return it.value();
}

bool SharedState::greiftAn(const QJsonObject& d) const
{
    // Deployt der Fahrer gerade oder haelt er noch ordentlich Ladung?
    return ji(d, "ers_mode") >= ERS_MODE_AN || jd(d, "ers_pct") >= ERS_PCT_AN;
}

// ── Ergebnis stehen lassen (Quali -> Rennen) ─────────────────────────────────
QJsonObject SharedState::maybeHoldQuali(const QJsonObject& data)
{
    // Portierung von maybeHoldQuali in core.js. Ohne das waere direkt nach dem
    // Ende der Quali eine leere Tabelle zu sehen, weil das Spiel schon auf die
    // naechste Session umschaltet.
    const QJsonObject session = data.value(QLatin1String("session")).toObject();
    const double now = nowMs();
    const bool connected = jb(data, "connected");
    const bool isQuali = jb(session, "is_quali");
    const QJsonArray drivers = data.value(QLatin1String("drivers")).toArray();

    if (connected && isQuali && !drivers.isEmpty()) {
        m_qLiveDrivers = drivers;
        m_qLiveSession = session;
        m_qWasQuali = true;
        m_qHaveSnap = false;
        return data;
    }

    if (m_qWasQuali && !isQuali && !m_qHaveSnap && !m_qLiveDrivers.isEmpty()) {
        m_qSnap = m_qLiveDrivers;
        m_qHaveSnap = true;
        m_qSince = now;
    }

    if (!m_qHaveSnap)
        return data;

    const QJsonObject sl = data.value(QLatin1String("start_lights")).toObject();
    const bool lights = (ji(sl, "num") > 0 || jb(sl, "out")) && jd(sl, "age", 9999) < 30;
    const bool racing = connected && !isQuali && ji(session, "current_lap") >= 1;
    const int holds = m_cfg.i("holds") > 0 ? m_cfg.i("holds") : 300;
    const bool expired = (now - m_qSince) > holds * 1000.0;

    if (lights || racing || expired) {
        m_qHaveSnap = false;
        m_qWasQuali = false;
        return data;
    }

    QJsonObject heldSession = m_qLiveSession;
    heldSession[QLatin1String("_quali_result")] = true;
    heldSession[QLatin1String("safety_car_status")] = QStringLiteral("none");
    heldSession[QLatin1String("time_left")] = 0;
    QJsonObject out = data;
    out[QLatin1String("drivers")] = m_qSnap;
    out[QLatin1String("connected")] = true;
    out[QLatin1String("focus_index")] = -1;
    out[QLatin1String("race_control")] = QJsonArray();
    out[QLatin1String("session")] = heldSession;
    return out;
}

// ── Quali-Status eines Fahrers ───────────────────────────────────────────────
QString SharedState::qualiStatus(const QJsonObject& d) const
{
    // Die vier nummerierten Faelle im Original stehen hier als dieselben
    // Kommentare - die Logik ist ueber Monate an echten Sessions geradegezogen.
    const int ds = ji(d, "driver_status");
    QString st;
    if (jb(d, "in_pit"))
        st = QStringLiteral("box");
    else if (ds == 3)
        st = QStringLiteral("out");
    else if (ds == 2)
        st = QStringLiteral("inlap");
    else if (ds == 1 || ds == 4)
        st = QStringLiteral("track");
    else
        st = QStringLiteral("box");
    if (st != QLatin1String("track"))
        return st;

    // (1) Ungueltige Runde ist nie ein Hotlap.
    if (jb(d, "lap_invalid"))
        return QStringLiteral("inlap");

    const auto pbIt = m_drvSecBest.constFind(ji(d, "index"));
    const double clt = jd(d, "current_lap_time");

    bool tooSlow = false;   // klar langsamer als die Bestpace -> Runde hin
    bool onPace = false;    // fertiger Sektor nahe der Bestpace -> bewiesen schnell
    if (pbIt != m_drvSecBest.cend()) {
        const std::array<double, 3>& pb = pbIt.value();
        const double s3 = s3Live(d);
        const std::array<double, 3> live{jd(d, "sector1"), jd(d, "sector2"), s3};
        double ab = 0.0;
        bool have = false;
        for (int i = 0; i < 3; ++i) {
            if (live[i] > 0 && pb[i] < INF) {
                ab += live[i] - pb[i];
                have = true;
            }
        }
        if (have) {
            if (ab > TOL)
                tooSlow = true;
            else
                onPace = true;
        }
        // LIVE, schon MITTEN im Sektor ueber der Bestpace bis zum Sektorende.
        if (clt > 0.5) {
            double cum = 0.0;
            bool ok = true;
            const int upto = std::min(ji(d, "sector"), 2);
            for (int i = 0; i <= upto; ++i) {
                if (pb[i] < INF)
                    cum += pb[i];
                else
                    ok = false;
            }
            if (ok && clt > cum + TOL)
                tooSlow = true;
        }
    }

    // (2a) ERS ENTLASTET. Wer gerade deployt oder noch ordentlich Ladung hat,
    // faehrt keine Auslaufrunde - dann bleibt es beim Hotlap, auch wenn die Pace
    // (noch) nicht passt. Verglichen wird gegen die SUMME der persoenlichen
    // Bestsektoren aus verschiedenen Runden - diese theoretische Bestzeit faehrt
    // niemand je, deshalb konnte sogar eine Runde mit Verbesserung als
    // "abgebrochen" gelten. Wenig ERS allein heisst dagegen NIE inlap.
    if (tooSlow && greiftAn(d))
        tooSlow = false;

    if (tooSlow)
        return QStringLiteral("inlap");                 // (2) klar zu langsam
    if (onPace)
        return QStringLiteral("track");                 // (3) bewiesen schnell
    // (4) Frischer Start ohne Sektor-Beweis: fliegende Runde / am Deployen / genug ERS.
    if (ds == 1 || greiftAn(d))
        return QStringLiteral("track");
    return QStringLiteral("out");
}

// ── Sektor-Buchhaltung ───────────────────────────────────────────────────────
void SharedState::mergeBestSectors(const QJsonObject& d)
{
    // Bestsektoren vom SPIEL (Paket 11) uebernehmen - alles vor dem Einschalten
    // fehlt sonst. Zusammengefuehrt per Minimum.
    const QJsonArray bs = d.value(QLatin1String("best_sectors")).toArray();
    if (bs.isEmpty())
        return;
    std::array<double, 3>& p = pb(ji(d, "index"));
    for (int i = 0; i < 3; ++i) {
        const double v = i < bs.size() ? bs.at(i).toDouble() : 0.0;
        if (v <= 0)
            continue;
        if (v < p[i])
            p[i] = v;
        if (v < m_secBest[i])
            m_secBest[i] = v;
    }
}

void SharedState::updateRaceBest(int idx, const std::array<double, 3>& secs)
{
    // Persoenliche Bestsektoren auch im Rennen fuehren (Basis fuer den Delta-Balken).
    std::array<double, 3>& p = pb(idx);
    for (int i = 0; i < 3; ++i)
        if (secs[i] > 0 && secs[i] < p[i])
            p[i] = secs[i];
}

void SharedState::absorbSectors(int idx, const std::array<double, 3>& secs)
{
    // Live gefahrene Sektoren in Session- und Fahrer-Bestzeit einrechnen.
    std::array<double, 3>& p = pb(idx);
    for (int i = 0; i < 3; ++i) {
        const double v = secs[i];
        if (v <= 0)
            continue;
        if (v < m_secBest[i])
            m_secBest[i] = v;
        if (v < p[i])
            p[i] = v;
    }
}

void SharedState::trackStints(const QList<QJsonObject>& drivers)
{
    // Stint-Laengen mitschreiben (Chips im Lower-Third).
    static const QString erlaubt = QStringLiteral("SMHIWE");
    for (const QJsonObject& d : drivers) {
        const QString comp = js(d, "compound");
        if (comp.isEmpty() || !erlaubt.contains(comp))
            continue;
        const int idx = ji(d, "index");
        const auto prevIt = m_prevComp.constFind(idx);
        if (prevIt != m_prevComp.cend() && prevIt.value() != comp) {
            QList<Stint>& lst = m_stintLen[idx];
            lst.append(Stint{prevIt.value(), m_prevAge.value(idx, 0)});
            if (lst.size() > 6)
                lst.removeFirst();
        }
        m_prevComp[idx] = comp;
        m_prevAge[idx] = ji(d, "tyre_age");
    }
}

void SharedState::resetSession()
{
    m_secBest = {INF, INF, INF};
    m_drvSecBest.clear();
    m_bestLapSecs.clear();
    m_prevLastLap.clear();
    m_lastQualiSecs.clear();
    m_curS3.clear();
    m_stintLen.clear();
    m_prevComp.clear();
    m_prevAge.clear();
    m_gapHist.clear();
    m_lapHist.clear();
    m_gapLastLap = 0;
    m_scRestartUntilLap = 0;
    m_scOnboardActive = false;
}

void SharedState::resetAll()
{
    resetSession();
    m_haveLastSecType = false;
    m_lastSecType = -1;
    m_haveLastSc = false;
    m_lastScStatus.clear();
    m_qHaveSnap = false;
    m_qSnap = QJsonArray();
    m_qLiveDrivers = QJsonArray();
    m_qLiveSession = QJsonObject();
    m_qWasQuali = false;
    m_qSince = 0.0;
    m_lastDrivers.clear();
    m_lastLapCount = 0;
}

// ── Hauptdurchlauf ───────────────────────────────────────────────────────────
DeriveResult SharedState::derive(const QJsonObject& data)
{
    DeriveResult out;
    const QJsonObject session = data.value(QLatin1String("session")).toObject();
    const bool connected = jb(data, "connected");

    // Session-Wechsel -> alle Bestzeiten und Verlaeufe zuruecksetzen
    const int secType = ji(session, "type", -1);
    if (!m_haveLastSecType || secType != m_lastSecType) {
        m_haveLastSecType = true;
        m_lastSecType = secType;
        resetSession();
    }

    // Safety-Car-Statuswechsel -> Onboard-Fenster
    const QString scStatus = connected ? js(session, "safety_car_status") : QStringLiteral("none");
    if (connected && (!m_haveLastSc || scStatus != m_lastScStatus)) {
        const bool scNow = scStatus == QLatin1String("sc") || scStatus == QLatin1String("vsc");
        const bool scPrev = m_lastScStatus == QLatin1String("sc") || m_lastScStatus == QLatin1String("vsc");
        if (scNow) {
            if (!jb(session, "is_quali"))
                m_scOnboardActive = true;
        } else if (scStatus == QLatin1String("none") && scPrev) {
            // Restart: Onboard auf P1, bis der Fuehrende ueber Start/Ziel ist
            const QJsonArray arr = data.value(QLatin1String("drivers")).toArray();
            for (const QJsonValue& v : arr) {
                const QJsonObject x = v.toObject();
                if (ji(x, "position") == 1) {
                    m_scRestartUntilLap = ji(x, "lap_num") + 1;
                    break;
                }
            }
        }
        out.scChanged = true;
        out.scNow = scStatus;
        out.scPrev = m_lastScStatus;
        m_haveLastSc = true;
        m_lastScStatus = scStatus;
    }

    // Nach Position sortieren - alle Bausteine sehen dieselbe Reihenfolge.
    QList<QJsonObject> drivers;
    {
        const QJsonArray arr = data.value(QLatin1String("drivers")).toArray();
        drivers.reserve(arr.size());
        for (const QJsonValue& v : arr)
            drivers.append(v.toObject());
        std::stable_sort(drivers.begin(), drivers.end(), [](const QJsonObject& a, const QJsonObject& b) {
            const int pa = ji(a, "position") > 0 ? ji(a, "position") : 999;
            const int pbb = ji(b, "position") > 0 ? ji(b, "position") : 999;
            if (pa != pbb)
                return pa < pbb;
            return ji(a, "index") < ji(b, "index");
        });
    }
    const int rows = m_cfg.i("rows");
    if (rows > 0 && drivers.size() > rows)
        drivers = drivers.mid(0, rows);

    // Gap-/Rundenzeiten-Verlauf: pro neuer Runde ein Schnappschuss
    const bool isQuali = jb(session, "is_quali");
    if (!isQuali && connected) {
        const int gl = ji(session, "current_lap");
        if (gl > 0 && gl != m_gapLastLap) {
            m_gapLastLap = gl;
            QHash<int, double> snap, lapSnap;
            for (const QJsonObject& d : drivers) {
                if (jb(d, "dnf") || jb(d, "dsq"))
                    continue;
                const int idx = ji(d, "index");
                snap[idx] = std::round(jd(d, "gap_to_leader") * 100.0) / 100.0;
                const double last = jd(d, "last_lap");
                if (last > 0)
                    lapSnap[idx] = std::round(last * 1000.0) / 1000.0;
            }
            m_gapHist[gl] = snap;
            m_lapHist[gl] = lapSnap;
        }
    }

    for (const QJsonObject& d : drivers) {
        mergeBestSectors(d);
        const int idx = ji(d, "index");
        const double s3 = s3Live(d);
        const double lastLap = jd(d, "last_lap");
        const double s1 = jd(d, "sector1");
        const double s2 = jd(d, "sector2");

        // Rundenende: beim Ueberfahren der Start/Ziel-Linie setzt das Spiel
        // sector1/sector2 der NEUEN Runde schon auf 0 -> fuer S3 der GERADE
        // beendeten Runde die zuletzt gesehenen S1/S2 nehmen.
        if (isQuali && lastLap > 0
            && (!m_prevLastLap.contains(idx) || m_prevLastLap.value(idx) != lastLap)) {
            m_prevLastLap[idx] = lastLap;
            const auto lsIt = m_lastQualiSecs.constFind(idx);
            const double s1c = s1 > 0 ? s1 : (lsIt != m_lastQualiSecs.cend() ? lsIt.value()[0] : 0.0);
            const double s2c = s2 > 0 ? s2 : (lsIt != m_lastQualiSecs.cend() ? lsIt.value()[1] : 0.0);
            const double s3c = (s1c > 0 && s2c > 0) ? (lastLap - s1c - s2c) : 0.0;
            if (s3c > 0) {
                m_curS3[idx] = s3c;                       // fuers Onboard
                std::array<double, 3>& p = pb(idx);
                if (s3c < p[2])
                    p[2] = s3c;
                if (s3c < m_secBest[2])
                    m_secBest[2] = s3c;
                if (std::abs(lastLap - jd(d, "best_lap")) < 0.005)
                    m_bestLapSecs[idx] = {s1c, s2c, s3c};
            }
        }

        // Zuletzt gueltige S1/S2 merken -> ueberlebt den Start/Ziel-Reset oben.
        if (s1 > 0 && s2 > 0)
            m_lastQualiSecs[idx] = {s1, s2};

        if (isQuali) {
            // Wie im Tower: nur auf einem echten Hotlap fliessen die LIVE-Sektoren
            // in die Bestzeiten.
            if (qualiStatus(d) == QLatin1String("track"))
                absorbSectors(idx, {s1, s2, s3});
        } else {
            updateRaceBest(idx, {s1, s2, s3});
        }
    }

    if (!isQuali && connected)
        trackStints(drivers);

    const int cur = ji(session, "current_lap");
    m_lastLapCount = cur > 0 ? cur : m_lastLapCount;
    m_lastDrivers = drivers;
    out.drivers = drivers;
    return out;
}

// ── Sektor-Anzeige (tower.js) ────────────────────────────────────────────────
SectorView SharedState::sectorView(const QJsonObject& d, bool isQuali, const QString& qualiSt)
{
    // Portierung von colorSectors / showBestSectors / clearSectorColors.
    // ⚠ Reihenfolge: derive() hat die Live-Sektoren SCHON in secBest/pb
    // eingerechnet, bevor hier verglichen wird. Ein neuer Bestsektor ist deshalb
    // immer lila (v <= v + eps) - genau wie im Web-Overlay.
    SectorView out;
    const int idx = ji(d, "index");

    if (isQuali && qualiSt != QLatin1String("track")) {
        // Box / Outlap / Inlap -> die Sektoren der schnellsten Runde des Fahrers.
        // Rueckfall auf die Bestsektoren, solange keine komplette Runde erfasst ist.
        std::array<double, 3> src{INF, INF, INF};
        if (m_bestLapSecs.contains(idx))
            src = m_bestLapSecs.value(idx);
        else if (m_drvSecBest.contains(idx))
            src = m_drvSecBest.value(idx);
        for (int i = 0; i < 3; ++i) {
            const double v = src[i];
            if (!(v > 0 && v < INF)) {
                out.times[i] = 0.0;
                out.classes[i].clear();
                continue;
            }
            out.times[i] = v;
            out.classes[i] = (v <= m_secBest[i] + SEC_EPS) ? QStringLiteral("sp") : QStringLiteral("sg");
        }
        return out;
    }

    out.times = {jd(d, "sector1"), jd(d, "sector2"), s3Live(d)};
    if (!isQuali)
        return out;                                   // im Rennen ohne Einfaerbung

    std::array<double, 3>& p = pb(idx);
    for (int i = 0; i < 3; ++i) {
        const double v = out.times[i];
        if (v <= 0) {
            out.classes[i].clear();
            continue;
        }
        if (v <= m_secBest[i] + SEC_EPS)
            out.classes[i] = QStringLiteral("sp");    // Session-Best
        else if (v <= p[i] + SEC_EPS)
            out.classes[i] = QStringLiteral("sg");    // persoenliche Bestzeit
        else
            out.classes[i] = QStringLiteral("sy");    // langsamer
        if (v < m_secBest[i])
            m_secBest[i] = v;
        if (v < p[i])
            p[i] = v;
    }
    return out;
}

}  // namespace kers::hud
