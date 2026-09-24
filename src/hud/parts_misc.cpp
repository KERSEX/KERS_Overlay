// Kleine Anzeigen: Startampel, Bestrunden-Banner, Gefahrenzone, Pit-Projektion.
#include "parts.h"

#include <algorithm>

namespace kers::hud {

// ── StartLights ──────────────────────────────────────────────────────────────
// STLG meldet die Zahl leuchtender Lichter (1..5), LGOT = alle aus. `age` ist die
// Zeit seit dem letzten Ereignis - danach blendet die Anzeige von selbst aus.
void StartLights::update(const QJsonObject& sl, bool connected, bool enabled)
{
    if (!enabled || sl.isEmpty() || !connected) {
        set_isVisible(false);
        set_lightsOut(false);
        return;
    }
    const int num = ji(sl, "num");
    const bool out = jb(sl, "out");
    const double age = jd(sl, "age", 9999);
    if (out) {
        const bool show = age < 3.0;
        set_isVisible(show);
        set_lightsOut(show);
        set_lit(0);
    } else if (num > 0 && age < 8.0) {
        set_isVisible(true);
        set_lightsOut(false);
        set_lit(num);
    } else {
        set_isVisible(false);
        set_lightsOut(false);
    }
}

// ── FastestLap ───────────────────────────────────────────────────────────────
FastestLap::FastestLap(Config& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() { set_isVisible(false); });
}

void FastestLap::update(const QJsonObject& session, bool connected, bool enabled)
{
    if (!enabled || !connected || jb(session, "is_quali")) {
        set_isVisible(false);
        return;
    }
    const double t = jd(session, "fastest_lap_time");
    if (!(t > 0))
        return;
    const QString sig = js(session, "fastest_lap_driver") + QLatin1Char('|') + QString::number(t, 'g', 17);
    if (m_haveSig && sig == m_sig)
        return;
    const bool first = !m_haveSig;   // mitten in der Session gestartet -> nicht feuern
    m_haveSig = true;
    m_sig = sig;
    if (first)
        return;
    set_name(js(session, "fastest_lap_driver"));
    set_lapTime(t);
    set_isVisible(true);
    const double dauer = m_cfg.d("flbdur") > 0 ? m_cfg.d("flbdur") : 4.5;
    m_timer.start(static_cast<int>(dauer * 1000));
}

// ── DangerZone ───────────────────────────────────────────────────────────────
void DangerZone::update(const QList<QJsonObject>& drivers, const QJsonObject& session, bool connected, bool enabled)
{
    const int type = ji(session, "type");
    const int cut = type == 5 ? 16 : (type == 6 ? 10 : 0);
    const double left = jd(session, "time_left");
    if (!enabled || !connected || !jb(session, "is_quali") || !cut || left <= 0 || left > 180) {
        set_isVisible(false);
        return;
    }
    const QJsonObject* bubble = nullptr;
    for (const QJsonObject& d : drivers) {
        if (ji(d, "position") == cut) {
            bubble = &d;
            break;
        }
    }
    if (!bubble) {
        set_isVisible(false);
        return;
    }
    set_cut(cut);
    set_name(js(*bubble, "name"));
    set_timeLeft(static_cast<int>(left));
    set_isVisible(true);
}

// ── PitProjection ────────────────────────────────────────────────────────────
void PitProjection::update(const QList<QJsonObject>& drivers, int focusIndex, const QJsonObject& session,
                           bool connected, bool enabled)
{
    if (!enabled || !connected || jb(session, "is_quali")) {
        set_isVisible(false);
        return;
    }
    const QJsonObject* d = nullptr;
    for (const QJsonObject& x : drivers) {
        if (ji(x, "index") == focusIndex) {
            d = &x;
            break;
        }
    }
    const int dmg = d ? std::max({ji(*d, "dmg_fl"), ji(*d, "dmg_fr"), ji(*d, "dmg_rw")}) : 0;
    if (!d || jb(*d, "dnf") || jb(*d, "dsq") || jb(*d, "in_pit") || dmg < DMG_MIN) {
        set_isVisible(false);
        return;
    }
    const double projGap = jd(*d, "gap_to_leader") + PIT_LOSS;
    int projPos = 1;
    for (const QJsonObject& o : drivers)
        if (ji(o, "index") != focusIndex && !jb(o, "dnf") && !jb(o, "dsq") && jd(o, "gap_to_leader") < projGap)
            ++projPos;
    set_name(js(*d, "name"));
    set_current(ji(*d, "position"));
    set_projected(projPos);
    set_isVisible(true);
}

}  // namespace kers::hud
