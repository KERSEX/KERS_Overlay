// Onboard-Telemetrie und Lower-Third.
#include "parts.h"

#include "models.h"
#include "theme.h"

#include <algorithm>

namespace kers::hud {

namespace {

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

const QJsonObject* findeIndex(const QList<QJsonObject>& drivers, int idx)
{
    for (const QJsonObject& d : drivers)
        if (ji(d, "index") == idx)
            return &d;
    return nullptr;
}

const QJsonObject* findePosition(const QList<QJsonObject>& drivers, int pos)
{
    for (const QJsonObject& d : drivers)
        if (ji(d, "position") == pos)
            return &d;
    return nullptr;
}

}  // namespace

// ── Onboard ──────────────────────────────────────────────────────────────────
// Quali: der Fahrer, auf den die Kamera schaut. Rennen: nur im Safety-Car-
// Fenster, und dann IMMER P1 - unabhaengig davon, wen die Kamera zeigt.
Onboard::Onboard(SharedState& shared, Config& cfg, QObject* parent)
    : QObject(parent)
    , m_shared(shared)
    , m_cfg(cfg)
{
}

void Onboard::update(const QList<QJsonObject>& drivers, int focusIndex, bool connected,
                     const QJsonObject& session, bool enabled)
{
    SharedState& sh = m_shared;
    const bool raceWindow = sh.scOnboardActive() || sh.scRestartUntilLap() > 0;
    const bool isQuali = jb(session, "is_quali");
    const bool visible = enabled && connected && (isQuali ? focusIndex >= 0 : raceWindow);
    if (!visible) {
        set_isVisible(false);
        return;
    }

    const QJsonObject* d = nullptr;
    if (isQuali) {
        d = findeIndex(drivers, focusIndex);
    } else {
        d = findePosition(drivers, 1);
        // Fenster schliesst, wenn P1 nach dem Restart ueber Start/Ziel ist.
        if (sh.scRestartUntilLap() > 0 && d && ji(*d, "lap_num") >= sh.scRestartUntilLap()) {
            sh.setScRestartUntilLap(0);
            sh.setScOnboardActive(false);
            set_isVisible(false);
            return;
        }
    }
    if (!d) {
        set_isVisible(false);
        return;
    }

    const int gear = ji(*d, "gear");
    const bool onHotlap = isQuali && sh.qualiStatus(*d) == QLatin1String("track");
    const int idx = ji(*d, "index");

    // Delta zur persoenlichen Bestrunde - nur in der Quali auf einem Hotlap.
    double delta = 0.0;
    bool have = false;
    const auto pbIt = sh.drvSecBest().constFind(idx);
    if (onHotlap && pbIt != sh.drvSecBest().cend()) {
        const std::array<double, 3>& pb = pbIt.value();
        const int done = std::min(ji(*d, "sector"), 2);
        const double secs[2] = {jd(*d, "sector1"), jd(*d, "sector2")};
        for (int i = 0; i < done; ++i) {
            if (secs[i] > 0 && pb[i] < INF) {
                delta += secs[i] - pb[i];
                have = true;
            }
        }
    }

    set_isVisible(true);
    set_name(js(*d, "name"));
    set_position(ji(*d, "position"));
    set_teamColor(Theme::teamColors().value(js(*d, "team"), QStringLiteral("#ffffff")));
    set_teamLogo(teamLogos().value(js(*d, "team")));
    set_speed(ji(*d, "speed"));
    set_gear(gear == -1 ? QStringLiteral("R") : (gear == 0 ? QStringLiteral("N") : QString::number(gear)));
    set_throttle(jd(*d, "throttle"));
    set_brake(jd(*d, "brake"));
    set_deltaVisible(m_cfg.b("deltabar") && onHotlap);
    set_delta(delta);
    set_hasDelta(have);
    set_ampVisible(onHotlap && m_cfg.b("ampel"));

    // Sektor-Ampel: S1/S2 live, S3 aus der zuletzt beendeten Runde.
    if (ampVisible()) {
        const int done = std::min(ji(*d, "sector"), 2);
        const double s3 = ji(*d, "sector") >= 2 ? 0.0 : sh.curS3().value(idx, 0.0);
        const double secs[3] = {jd(*d, "sector1"), jd(*d, "sector2"), s3};
        QVariantList amp;
        for (int i = 0; i < 3; ++i) {
            const double v = secs[i];
            if (v > 0) {
                const double pbv = pbIt != sh.drvSecBest().cend() ? pbIt.value()[i] : INF;
                if (v <= sh.secBest()[i] + 0.005)
                    amp.append(QStringLiteral("sp"));
                else if (v <= pbv + 0.005)
                    amp.append(QStringLiteral("sg"));
                else
                    amp.append(QStringLiteral("sy"));
            } else if (i == done) {
                amp.append(QStringLiteral("run"));
            } else {
                amp.append(QString());
            }
        }
        set_amp(amp);
    }
}

// ── LowerThird ───────────────────────────────────────────────────────────────
LowerThird::LowerThird(SharedState& shared, Config& cfg, QObject* parent)
    : QObject(parent)
    , m_shared(shared)
    , m_cfg(cfg)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() { set_isVisible(false); });
}

void LowerThird::update(const QList<QJsonObject>& drivers, int focusIndex, const QJsonObject& session,
                        bool connected, bool enabled)
{
    if (!enabled || !connected || jb(session, "is_quali") || ji(session, "current_lap") < 3) {
        set_isVisible(false);
        m_haveFocus = true;
        m_focus = focusIndex;
        return;
    }
    if (m_haveFocus && focusIndex == m_focus)
        return;                          // nur beim WECHSEL einblenden
    m_haveFocus = true;
    m_focus = focusIndex;
    const QJsonObject* d = findeIndex(drivers, focusIndex);
    if (!d)
        return;

    const QString compound = js(*d, "compound");
    const int age = ji(*d, "tyre_age");
    const int lapsDown = ji(*d, "laps_down");
    const double gapAhead = jd(*d, "gap_to_ahead");
    QString gap;
    if (ji(*d, "position") == 1)
        gap = QStringLiteral("LEADER");
    else if (lapsDown >= 1)
        gap = QStringLiteral("+%1 LAP%2").arg(lapsDown).arg(lapsDown > 1 ? QStringLiteral("S") : QString());
    else if (gapAhead > 0)
        gap = QStringLiteral("+%1").arg(gapAhead, 0, 'f', 3);
    else
        gap = QStringLiteral("0.000");

    // Reifenfarben fuer Mischungen ohne eigenes Bild
    static const QHash<QString, QString> tyreHex = {
        {"S", "#E8002D"}, {"M", "#FFC906"}, {"H", "#EBEBEB"}, {"I", "#39B54A"}, {"W", "#0067FF"},
    };
    QVariantList stints;
    if (m_cfg.b("strat") && !compound.isEmpty()) {
        QList<Stint> raw = m_shared.stintLen().value(ji(*d, "index"));
        raw.append(Stint{compound, age});
        for (const Stint& s : raw) {
            if (s.compound.isEmpty())
                continue;
            QVariantMap m;
            m.insert("icon", istReifenIcon(s.compound) ? s.compound : QString());
            m.insert("letter", s.compound);
            m.insert("color", tyreHex.value(s.compound, QStringLiteral("#888888")));
            m.insert("laps", s.laps);
            stints.append(m);
        }
    }
    set_stints(stints);

    set_name(js(*d, "name"));
    set_position(ji(*d, "position"));
    set_team(js(*d, "team"));
    set_teamColor(Theme::teamColors().value(js(*d, "team"), QStringLiteral("#ffffff")));
    set_tyreIcon(istReifenIcon(compound) ? compound : QString());
    set_ageText((!compound.isEmpty() && age >= 0) ? QStringLiteral(u"· Rnd %1").arg(age) : QString());
    set_gapText(gap);
    set_isVisible(true);
    const double dauer = m_cfg.d("ltdur") > 0 ? m_cfg.d("ltdur") : 4.0;
    m_timer.start(static_cast<int>(dauer * 1000));
}

}  // namespace kers::hud
