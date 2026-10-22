// Hotlap-Boxen: wer faehrt gerade eine fliegende Runde (hotlap.js).
//
// Sortiert nach Streckenfortschritt: wer als Naechstes ueber die Linie kommt,
// steht links. Hoechstens vier Boxen.
//
// Ab 0.3.1 dazu, fuer die F1-TV-Karten (das eingebaute Design liest es nicht):
// die Zielzeit wie im Fernsehen. Wer gerade weiterkaeme, faehrt gegen die Pole,
// wer ausscheiden wuerde (oder noch keine Zeit hat) gegen die Zeit an der
// Cut-Linie - und zwar als Zwischenzeit am NAECHSTEN Sektor. Nach der Linie
// steht das Delta auf genau dieses Ziel.
#include "parts.h"

#include "models.h"
#include "theme.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

const QStringList& hotlapRoles()
{
    static const QStringList roles = {
        "slot", "driverIndex", "position", "name", "teamColor", "teamLogo",
        "tyreIcon", "tyreStamp", "invalid", "timeBase", "timeAt", "ticking",
        "staticTime", "delta", "deltaUp", "hasDelta",
        "sectors", "sectorClasses",
        "focused", "finished", "lapClass", "finalDelta", "hasFinal",
        "targetTime", "targetName", "targetSector",
    };
    return roles;
}

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

/// Ist der Fahrer seit dem letzten Stand ueber die Start/Ziel-Linie?
bool rundeVorbei(const QJsonObject& vorher, const QJsonObject& jetzt)
{
    const int lv = ji(vorher, "lap_num");
    const int lj = ji(jetzt, "lap_num");
    if (lv > 0 && lj > 0)
        return lj > lv;
    // Ohne Rundenzaehler (aeltere Aufnahmen): die Uhr ist zurueckgesprungen.
    const double cv = jd(vorher, "current_lap_time");
    return cv > 10.0 && jd(jetzt, "current_lap_time") + 5.0 < cv;
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
    const double now = m_uhr();
    QHash<int, const QJsonObject*> jetzt;
    for (const QJsonObject& x : drivers)
        jetzt.insert(ji(x, "index"), &x);

    // Wer beim letzten Stand auf seiner fliegenden Runde war und jetzt ueber die
    // Linie ist, wandert mit seiner Zeit in m_fertig - egal, ob er danach
    // abbiegt oder gleich die naechste fliegende Runde anhaengt.
    // ⚠ Bis 0.3.0 zaehlte nur das Abbiegen. Wer weiterfuhr, blieb "auf Hotlap",
    // und seine Karte sprang an der Linie sofort auf 0:00 zurueck - die
    // gefahrene Zeit war nie zu sehen. Umgekehrt zeigte eine ABGEBROCHENE Runde
    // (von der Liste gefallen, ohne die Linie zu kreuzen) vier Sekunden lang die
    // vorige Runde als "fertig". Die geht jetzt ohne Nachlauf.
    for (auto it = m_laeuft.cbegin(); it != m_laeuft.cend(); ++it) {
        const QJsonObject* j = jetzt.value(it.key());
        if (j && rundeVorbei(it.value(), *j))
            nachlaufStarten(it.key(), it.value(), *j, now);
    }
    m_laeuft.clear();
    for (const QJsonObject& d : hot)
        m_laeuft.insert(ji(d, "index"), d);

    for (auto it = m_fertig.begin(); it != m_fertig.end();) {
        if (it.value().bis <= now) {
            it = m_fertig.erase(it);
            continue;
        }
        Fertig& f = it.value();
        if (const QJsonObject* j = jetzt.value(it.key())) {
            // Zieht das Spiel last_lap erst ein Paket spaeter nach, gilt ab dann
            // diese Zeit statt der zuletzt gesehenen Uhr.
            const double l = jd(*j, "last_lap");
            if (!f.zeitFest && l > 0 && std::abs(l - f.alteLast) > 0.0005) {
                f.fahrer[QLatin1String("last_lap")] = l;
                f.fahrer[QLatin1String("_fertig_zeit")] = l;
                f.zeitFest = true;
            }
            // Die NEUE Position zeigen - genau das will man nach der Linie sehen.
            f.fahrer[QLatin1String("position")] = ji(*j, "position");
        }
        ++it;
    }
}

void Hotlaps::nachlaufStarten(int idx, const QJsonObject& vorher, const QJsonObject& jetzt, double now)
{
    // Nach dem Ueberfahren steht die gefahrene Runde in last_lap. Ist sie noch
    // nicht nachgezogen (gleich wie vorher), tut es vorlaeufig die zuletzt
    // laufende Zeit - nachlaufPflegen ersetzt sie, sobald sie kommt.
    const double alt = jd(vorher, "last_lap");
    double zeit = jd(jetzt, "last_lap");
    const bool fest = zeit > 0 && std::abs(zeit - alt) > 0.0005;
    if (!fest)
        zeit = jd(vorher, "current_lap_time");
    if (zeit <= 0)
        return;                               // nichts Sinnvolles zu zeigen
    // ⚠ Grundlage ist der Stand VOR der Linie. Danach hat das Spiel
    // sector1/sector2 schon fuer die neue Runde zurueckgesetzt - die
    // Nachlauf-Box zeigte deshalb dreimal "-", ausgerechnet bei der fertigen
    // Runde. last_lap wird auf die eben gefahrene Zeit gesetzt; damit rechnet
    // s3Live den dritten Sektor korrekt als Rest aus (Zeit - S1 - S2).
    Fertig f;
    f.fahrer = vorher;
    f.fahrer[QLatin1String("last_lap")] = zeit;
    f.fahrer[QLatin1String("_fertig_zeit")] = zeit;
    f.fahrer[QLatin1String("position")] = ji(jetzt, "position");
    f.bis = now + FINISH_HOLD_MS;
    // Der Platz wird MITGENOMMEN: die Box soll dort stehen bleiben, wo sie
    // war, und erst nach dem Nachlauf vom naechsten Fahrer uebernommen werden.
    f.platz = m_letzterPlatz.value(idx, MAX_BOXES);
    f.alteLast = alt;
    f.zeitFest = fest;
    // Das Ziel von VOR der Linie - danach zaehlt die neue Runde schon selbst mit
    // (holt er die Pole, waere sein Delta sonst immer 0).
    f.ziel = zielFuer(vorher);
    f.poleVorher = m_pole.runde;
    f.bestVorher = jd(vorher, "best_lap");
    m_fertig.insert(idx, f);
}

QList<QJsonObject> Hotlaps::mitNachlauf(QList<QJsonObject> hot) const
{
    // Im Nachlauf zeigt die Karte die fertige Runde - auch wenn der Fahrer schon
    // die naechste fliegende Runde faehrt. Die laufende kommt danach dran.
    hot.erase(std::remove_if(hot.begin(), hot.end(),
                             [this](const QJsonObject& d) { return m_fertig.contains(ji(d, "index")); }),
              hot.end());
    QList<int> wartend = m_fertig.keys();
    // Aufsteigend einsetzen, sonst verschieben sich die spaeteren Stellen.
    std::sort(wartend.begin(), wartend.end(),
              [this](int a, int b) { return m_fertig.value(a).platz < m_fertig.value(b).platz; });
    for (int idx : wartend) {
        const int platz = std::min(m_fertig.value(idx).platz, static_cast<int>(hot.size()));
        hot.insert(platz, m_fertig.value(idx).fahrer);
    }
    return hot;
}

// ── Zielzeit (F1-TV-Karten) ──────────────────────────────────────────────────
void Hotlaps::zieleBestimmen(const QList<QJsonObject>& drivers, int elimCut)
{
    QList<const QJsonObject*> mitZeit;
    for (const QJsonObject& d : drivers)
        if (jd(d, "best_lap") > 0)
            mitZeit.append(&d);
    std::stable_sort(mitZeit.begin(), mitZeit.end(), [](const QJsonObject* a, const QJsonObject* b) {
        return jd(*a, "best_lap") < jd(*b, "best_lap");
    });
    m_pole = mitZeit.isEmpty() ? Ziel{} : zielAus(*mitZeit.first());
    // Haben noch nicht so viele eine Zeit, kommt jede gueltige Runde weiter -
    // dann bleibt es bei der Pole.
    m_cut = (elimCut > 0 && mitZeit.size() >= elimCut) ? zielAus(*mitZeit.at(elimCut - 1)) : Ziel{};
}

Hotlaps::Ziel Hotlaps::zielAus(const QJsonObject& d) const
{
    Ziel z;
    z.runde = jd(d, "best_lap");
    z.name = js(d, "name");
    // Sektoren genau der Bestrunde: vom Server (Paket 11, ab 0.3.1), sonst die
    // an der Linie selbst mitgeschriebenen. Nur, wenn sie zur Rundenzeit passen.
    const auto passt = [&z](const std::array<double, 3>& s) {
        return s[0] > 0 && s[1] > 0 && s[2] > 0 && std::abs(s[0] + s[1] + s[2] - z.runde) < 0.02;
    };
    std::array<double, 3> s{0, 0, 0};
    const QJsonArray a = d.value(QLatin1String("best_lap_sectors")).toArray();
    for (int i = 0; i < 3 && i < a.size(); ++i)
        s[i] = a.at(i).toDouble();
    if (!passt(s)) {
        const auto it = m_shared.bestLapSecs().constFind(ji(d, "index"));
        if (it != m_shared.bestLapSecs().cend())
            s = it.value();
    }
    if (passt(s)) {
        z.sek = s;
        z.sekOk = true;
    }
    return z;
}

const Hotlaps::Ziel& Hotlaps::zielFuer(const QJsonObject& d) const
{
    // Wie die F1-TV-Grafik: wer gerade ausscheiden wuerde (oder noch keine Zeit
    // hat), faehrt gegen die Cut-Linie, alle anderen gegen die Pole.
    if (m_cut.runde > 0) {
        const double b = jd(d, "best_lap");
        if (!(b > 0) || b > m_cut.runde + 0.0005)
            return m_cut;
    }
    return m_pole;
}

void Hotlaps::update(const QList<QJsonObject>& drivers, int focusIndex, int elimCut, bool active)
{
    ++m_stamp;
    if (!active) {
        m_rows->clear();
        m_fertig.clear();
        m_laeuft.clear();
        m_pole = Ziel{};
        m_cut = Ziel{};
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

    // ⚠ Reihenfolge: der Nachlauf friert die Ziele von VOR diesem Stand ein,
    // erst danach kommen die neuen Bestzeiten in die Ziele.
    nachlaufPflegen(drivers, hot);
    zieleBestimmen(drivers, elimCut);
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
        v.insert("focused", focusIndex >= 0 && idx == focusIndex);

        const auto fIt = m_fertig.constFind(idx);
        if (fertig > 0 && fIt != m_fertig.cend()) {
            // Fertige Runde: Delta auf das Ziel von vor der Linie, dazu die
            // Farbe der Runde wie im Fernsehen (lila = neue Bestzeit der Session,
            // gruen = persoenliche Bestzeit).
            const Fertig& f = fIt.value();
            QString cls;
            if (!jb(d, "lap_invalid")) {
                if (!(f.poleVorher > 0) || fertig < f.poleVorher - 0.0005)
                    cls = QStringLiteral("sp");
                else if (!(f.bestVorher > 0) || fertig < f.bestVorher - 0.0005)
                    cls = QStringLiteral("sg");
            }
            v.insert("finished", true);
            v.insert("lapClass", cls);
            v.insert("hasFinal", f.ziel.runde > 0);
            v.insert("finalDelta", f.ziel.runde > 0 ? fertig - f.ziel.runde : 0.0);
            v.insert("targetTime", f.ziel.runde);
            v.insert("targetName", f.ziel.name);
            v.insert("targetSector", 3);
        } else {
            // Laufende Runde: Ziel am naechsten Sektor (1, 2 oder die Linie).
            // Ohne die Sektoren der Bezugsrunde bleibt nur deren Rundenzeit.
            const Ziel& z = zielFuer(d);
            const int naechster = secs[0] > 0 ? (secs[1] > 0 ? 2 : 1) : 0;
            double t = z.runde;
            int sektor = 3;
            if (naechster < 2 && z.sekOk) {
                t = z.sek[0] + (naechster == 1 ? z.sek[1] : 0.0);
                sektor = naechster + 1;
            }
            v.insert("finished", false);
            v.insert("lapClass", QString());
            v.insert("hasFinal", false);
            v.insert("finalDelta", 0.0);
            v.insert("targetTime", z.runde > 0 ? t : 0.0);
            v.insert("targetName", z.name);
            v.insert("targetSector", sektor);
        }
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
