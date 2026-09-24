// Hotlap-Boxen: wer faehrt gerade eine fliegende Runde (hotlap.js).
//
// Sortiert nach Streckenfortschritt: wer als Naechstes ueber die Linie kommt,
// steht links. Hoechstens vier Boxen.
#include "parts.h"

#include "models.h"
#include "theme.h"

#include <QSet>

#include <algorithm>

namespace kers::hud {

namespace {

const QStringList& hotlapRoles()
{
    static const QStringList roles = {
        "slot", "driverIndex", "position", "name", "teamColor", "teamLogo",
        "tyreIcon", "tyreStamp", "invalid", "timeBase", "timeAt", "ticking",
        "staticTime", "delta", "deltaUp", "hasDelta",
        "sectors", "sectorClasses",
    };
    return roles;
}

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

}  // namespace

Hotlaps::Hotlaps(SharedState& shared, Config& cfg, QObject* parent)
    : QObject(parent)
    , m_shared(shared)
    , m_cfg(cfg)
    , m_rows(new SlotModel(hotlapRoles(), this))
{
}

int Hotlaps::boxWunsch() const
{
    return std::clamp(m_cfg.i("hotlapboxes"), 0, MAX_BOXES);
}

// ── Nachlauf: die fertige Zeit noch kurz stehen lassen ───────────────────────
void Hotlaps::nachlaufPflegen(const QList<QJsonObject>& drivers, const QList<QJsonObject>& hot)
{
    // Wer gerade ueber die Linie ist, wandert mit seiner Zeit in m_fertig.
    const double now = wallMs();
    QSet<int> aktuell;
    for (const QJsonObject& d : hot) {
        aktuell.insert(ji(d, "index"));
        m_laeuft[ji(d, "index")] = d;
    }

    QList<int> raus;
    for (auto it = m_laeuft.cbegin(); it != m_laeuft.cend(); ++it)
        if (!aktuell.contains(it.key()))
            raus.append(it.key());
    for (int idx : raus) {
        const QJsonObject vorher = m_laeuft.take(idx);
        const QJsonObject* jetzt = nullptr;
        for (const QJsonObject& x : drivers) {
            if (ji(x, "index") == idx) {
                jetzt = &x;
                break;
            }
        }
        // Nach dem Ueberfahren steht die gefahrene Runde in last_lap. Ist sie
        // noch nicht nachgezogen, tut es die zuletzt laufende Zeit.
        double zeit = jetzt ? jd(*jetzt, "last_lap") : 0.0;
        if (zeit <= 0)
            zeit = jd(vorher, "current_lap_time");
        if (zeit <= 0)
            continue;                         // nichts Sinnvolles zu zeigen
        // ⚠ Grundlage ist der Stand VOR der Linie. Danach hat das Spiel
        // sector1/sector2 schon fuer die neue Runde zurueckgesetzt - die
        // Nachlauf-Box zeigte deshalb dreimal "-", ausgerechnet bei der fertigen
        // Runde. last_lap wird auf die eben gefahrene Zeit gesetzt; damit rechnet
        // s3Live den dritten Sektor korrekt als Rest aus (Zeit - S1 - S2).
        QJsonObject kopie = vorher;
        kopie[QLatin1String("last_lap")] = zeit;
        kopie[QLatin1String("_fertig_zeit")] = zeit;
        // Der Platz wird MITGENOMMEN: die Box soll dort stehen bleiben, wo sie
        // war, und erst nach den 4 s vom naechsten Fahrer uebernommen werden.
        m_fertig[idx] = Fertig{kopie, now + FINISH_HOLD_MS, m_letzterPlatz.value(idx, MAX_BOXES)};
    }

    // Abgelaufen - oder der Fahrer faehrt schon wieder eine fliegende Runde.
    for (auto it = m_fertig.begin(); it != m_fertig.end();) {
        if (it.value().bis <= now || aktuell.contains(it.key()))
            it = m_fertig.erase(it);
        else
            ++it;
    }
}

QList<QJsonObject> Hotlaps::mitNachlauf(QList<QJsonObject> hot) const
{
    // Wartende Zeiten an ihrer alten Stelle wieder einsetzen.
    QSet<int> drin;
    for (const QJsonObject& d : hot)
        drin.insert(ji(d, "index"));
    QList<int> wartend;
    for (auto it = m_fertig.cbegin(); it != m_fertig.cend(); ++it)
        if (!drin.contains(it.key()))
            wartend.append(it.key());
    // Aufsteigend einsetzen, sonst verschieben sich die spaeteren Stellen.
    std::sort(wartend.begin(), wartend.end(),
              [this](int a, int b) { return m_fertig.value(a).platz < m_fertig.value(b).platz; });
    for (int idx : wartend) {
        const int platz = std::min(m_fertig.value(idx).platz, static_cast<int>(hot.size()));
        hot.insert(platz, m_fertig.value(idx).fahrer);
    }
    return hot;
}

void Hotlaps::update(const QList<QJsonObject>& drivers, int focusIndex, bool active)
{
    ++m_stamp;
    if (!active) {
        m_rows->clear();
        m_fertig.clear();
        m_laeuft.clear();
        return;
    }
    QList<QJsonObject> hot;
    for (const QJsonObject& d : drivers)
        if (m_shared.qualiStatus(d) == QLatin1String("track") && !jb(d, "dnf") && !jb(d, "dsq"))
            hot.append(d);
    std::stable_sort(hot.begin(), hot.end(),
                     [](const QJsonObject& a, const QJsonObject& b) { return jd(a, "lap_distance") > jd(b, "lap_distance"); });

    // Der beobachtete Fahrer steht links - nur wenn er wirklich eine fliegende
    // Runde faehrt; sonst bleibt es bei "wer zuerst ueber die Linie kommt".
    if (focusIndex >= 0) {
        for (int i = 0; i < hot.size(); ++i) {
            if (ji(hot.at(i), "index") == focusIndex) {
                if (i)
                    hot.move(i, 0);
                break;
            }
        }
    }

    nachlaufPflegen(drivers, hot);
    hot = mitNachlauf(hot);
    const int wunsch = boxWunsch();
    if (hot.size() > wunsch)
        hot = hot.mid(0, wunsch);
    m_letzterPlatz.clear();
    for (int i = 0; i < hot.size(); ++i)
        m_letzterPlatz[ji(hot.at(i), "index")] = i;

    QList<SlotModel::Entry> entries;
    const std::array<double, 3>& secBest = m_shared.secBest();
    for (const QJsonObject& d : hot) {
        const int idx = ji(d, "index");
        const std::array<double, 3> secs{jd(d, "sector1"), jd(d, "sector2"), s3Live(d)};
        int done = 0;
        for (double v : secs)
            if (v > 0)
                ++done;

        // Live-Delta gegen die Session-Bestsektoren bis zum fertigen Sektor.
        double delta = 0.0;
        bool have = false;
        for (int i = 0; i < done; ++i) {
            if (secBest[i] < INF) {
                delta += secs[i] - secBest[i];
                have = true;
            }
        }

        QString compound = js(d, "compound");
        if (compound.isEmpty())
            compound = QStringLiteral("?");
        const auto prev = m_tyre.constFind(idx);
        if (prev != m_tyre.cend() && prev.value().first != compound)
            m_tyre[idx] = {compound, m_stamp};
        else if (prev == m_tyre.cend())
            m_tyre[idx] = {compound, 0};

        // Nachlauf-Eintrag: die Uhr steht, gezeigt wird die gefahrene Zeit.
        const double fertig = jd(d, "_fertig_zeit");
        const double clt = fertig > 0 ? 0.0 : jd(d, "current_lap_time");
        QVariantList sectors, classes;
        for (int i = 0; i < 3; ++i) {
            sectors.append(secs[i]);
            classes.append(secClass(secs[i], idx, i));
        }

        QVariantMap v;
        v.insert("driverIndex", idx);
        v.insert("position", ji(d, "position"));
        v.insert("name", js(d, "name"));
        v.insert("teamColor", Theme::teamColors().value(js(d, "team"), QStringLiteral("#ffffff")));
        v.insert("teamLogo", teamLogos().value(js(d, "team")));
        v.insert("tyreIcon", istReifenIcon(compound) ? compound : QString());
        v.insert("tyreStamp", m_tyre.value(idx).second);
        v.insert("invalid", jb(d, "lap_invalid"));
        // Die grosse Zeit zaehlt in QML weiter: Basiswert + Zeitstempel, der
        // Rest ist Uhr. Genau wie tickHotlapTimes() im Original.
        v.insert("timeBase", clt);
        v.insert("timeAt", clt > 0 ? wallMs() : 0.0);
        v.insert("ticking", clt > 0);
        v.insert("staticTime", fertig > 0 ? fertig : jd(d, "last_lap"));
        v.insert("delta", delta);
        v.insert("deltaUp", delta <= 0.0005);
        v.insert("hasDelta", have);
        v.insert("sectors", sectors);
        v.insert("sectorClasses", classes);
        entries.append({idx, v});
    }
    m_rows->sync(entries);
}

QString Hotlaps::secClass(double v, int idx, int i) const
{
    // Nur LESEND - die Bestzeiten pflegt derive.cpp.
    if (v <= 0)
        return QString();
    if (v <= m_shared.secBest()[i] + 0.005)
        return QStringLiteral("sp");
    const auto pb = m_shared.drvSecBest().constFind(idx);
    if (pb != m_shared.drvSecBest().cend() && v <= pb.value()[i] + 0.005)
        return QStringLiteral("sg");
    return QStringLiteral("sy");
}

}  // namespace kers::hud
