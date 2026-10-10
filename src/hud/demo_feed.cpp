#include "demo_feed.h"

#include "jsonfile.h"
#include "paths.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

struct TeamDef {
    const char* team;
    const char* a;
    const char* b;
};

// 11 Teams = 22 Autos, wie das Feld ab 2026. Passt zu FULL_GRID im Tower.
const TeamDef kTeams[] = {
    {"Red Bull", "VERSTAPPEN", "TSUNODA"},
    {"McLaren", "NORRIS", "PIASTRI"},
    {"Ferrari", "LECLERC", "HAMILTON"},
    {"Mercedes", "RUSSELL", "ANTONELLI"},
    {"Aston Martin", "ALONSO", "STROLL"},
    {"Alpine", "GASLY", "COLAPINTO"},
    {"Williams", "ALBON", "SAINZ"},
    {"RB", "HADJAR", "LAWSON"},
    {"Audi", "HULKENBERG", "BORTOLETO"},
    {"Haas", "OCON", "BEARMAN"},
    {"Cadillac", "PEREZ", "BOTTAS"},
};

const char* const kCompounds[] = {"S", "M", "H"};

double uniform(double a, double b)
{
    return a + (b - a) * QRandomGenerator::global()->generateDouble();
}

int randint(int a, int b)
{
    return QRandomGenerator::global()->bounded(a, b + 1);
}

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

int compoundIndex(const QString& c)
{
    for (int i = 0; i < 3; ++i)
        if (c == QLatin1String(kCompounds[i]))
            return i;
    return 0;
}

}  // namespace

QJsonObject DemoFeed::realSettings()
{
    // Die echten Settings mitlesen, wenn es sie gibt - so sieht die Demo aus wie
    // das laufende Overlay, inklusive Branding und Deckkraft.
    return json::readObject(paths::dataFile(QStringLiteral("overlay_settings.json")));
}

QJsonArray DemoFeed::buildTrack(int n)
{
    // Eine erfundene, geschlossene Streckenkontur im Format von /api/track:
    // [x, z, Rundenanteil, Sektor]. Ueberlagerung zweier Kreisbewegungen.
    QJsonArray pts;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * M_PI * i / n;
        const double x = 620 * std::cos(a) + 140 * std::cos(3 * a);
        const double z = 430 * std::sin(a) + 160 * std::sin(2 * a);
        const double frac = static_cast<double>(i) / n;
        const int sector = frac < 1.0 / 3 ? 0 : (frac < 2.0 / 3 ? 1 : 2);
        pts.append(QJsonArray{x, z, std::round(frac * 10000.0) / 10000.0, sector});
    }
    return pts;
}

QJsonArray DemoFeed::trackPoint(double frac) const
{
    const int i = static_cast<int>(std::clamp(frac, 0.0, 0.9999) * m_track.size());
    const QJsonArray p = m_track.at(i).toArray();
    return QJsonArray{p.at(0).toDouble(), p.at(1).toDouble()};
}

DemoFeed::DemoFeed(int hz, bool quali, bool vorschau, QObject* parent)
    : Feed(parent)
    , m_quali(quali)
    , m_vorschau(vorschau)
{
    m_clock.start();
    m_timer.setInterval(std::max(16, 1000 / std::clamp(hz, 1, 60)));
    connect(&m_timer, &QTimer::timeout, this, &DemoFeed::emitPayload);

    int i = 0;
    for (const TeamDef& t : kTeams) {
        const char* names[2] = {t.a, t.b};
        for (int k = 0; k < 2; ++k) {
            const int idx = i * 2 + k;
            Driver d;
            d.index = idx;
            d.name = QString::fromLatin1(names[k]);
            d.team = QString::fromLatin1(t.team);
            // Grundtempo: vorne minimal schneller, plus etwas Zufall.
            d.pace = 1.0 + idx * 0.0016 + uniform(-0.0007, 0.0007);
            d.progress = static_cast<double>(22 - idx) * 0.4;
            d.compound = QString::fromLatin1(kCompounds[idx % 3]);
            d.tyreAge = randint(2, 18);
            d.grid = idx + 1;
            m_drivers.append(d);
        }
        ++i;
    }
    std::shuffle(m_drivers.begin(), m_drivers.end(), *QRandomGenerator::global());
    m_track = buildTrack();
    m_settings = realSettings();
}

QJsonObject DemoFeed::championship() const
{
    // Ein erfundener WM-Stand - Format wie /api/championship.
    QList<const Driver*> sorted;
    for (const Driver& d : m_drivers)
        sorted.append(&d);
    std::sort(sorted.begin(), sorted.end(), [](const Driver* a, const Driver* b) { return a->pace < b->pace; });
    QJsonArray rows;
    for (int i = 0; i < sorted.size(); ++i) {
        QJsonObject r;
        r["name"] = sorted.at(i)->name;
        r["team"] = sorted.at(i)->team;
        r["total_points"] = 310 - i * 17;
        r["live_points"] = std::max(0, 25 - i * 3);
        rows.append(r);
    }
    // Team-WM wie der Server sie rechnet: Summe je Team, sortiert
    QMap<QString, QPair<double, double>> summe;
    for (const QJsonValue& v : std::as_const(rows)) {
        const QJsonObject r = v.toObject();
        auto& s = summe[r.value("team").toString()];
        s.first += r.value("total_points").toDouble() - r.value("live_points").toDouble();
        s.second += r.value("live_points").toDouble();
    }
    QList<QJsonObject> teamListe;
    for (auto it = summe.begin(); it != summe.end(); ++it)
        teamListe.append(QJsonObject{{"team", it.key()}, {"base_points", it.value().first},
                                     {"live_points", it.value().second},
                                     {"total_points", it.value().first + it.value().second}});
    std::sort(teamListe.begin(), teamListe.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value("total_points").toDouble() > b.value("total_points").toDouble();
    });
    QJsonArray teams;
    for (const QJsonObject& t : teamListe)
        teams.append(t);
    QJsonObject out;
    out["title"] = QStringLiteral("Fahrerwertung");
    out["teams_title"] = QStringLiteral("Teamwertung");
    out["standings"] = rows;
    out["teams"] = teams;
    return out;
}

void DemoFeed::start()
{
    m_timer.start();
    emit linkChanged(true);
}

void DemoFeed::stop()
{
    m_timer.stop();
    emit linkChanged(false);
}

void DemoFeed::setHz(int hz)
{
    m_timer.setInterval(std::max(16, 1000 / std::clamp(hz, 1, 60)));
}

QString DemoFeed::demoChart(double t)
{
    // Nur "gap" und "lap": der Positionsverlauf braeuchte /api/lap_positions.
    const double cycle = std::fmod(t, 140.0);
    if (cycle >= 55 && cycle < 70)
        return QStringLiteral("gap");
    if (cycle >= 70 && cycle < 82)
        return QStringLiteral("lap");
    return QStringLiteral("none");
}

QJsonArray DemoFeed::raceControl(double t, bool redFlag, const QList<const Driver*>& order)
{
    // Ab und zu eine Meldung, damit der Banner etwas zu tun hat. Ids zaehlen hoch.
    if (redFlag && m_rcId % 7 != 6) {
        ++m_rcId;
        QJsonObject m;
        m["id"] = m_rcId;
        m["type"] = QStringLiteral("redflag");
        m["text"] = QStringLiteral("ROTE FLAGGE");
        return QJsonArray{m};
    }
    if (t < m_rcNext)
        return {};
    m_rcNext = t + 9.0;
    ++m_rcId;
    const QString who = order.at(m_rcId % std::min(8, static_cast<int>(order.size())))->name;
    QString kind, text;
    switch (m_rcId % 5) {
    case 0: kind = QStringLiteral("tracklimit"); text = QStringLiteral(u"TRACK LIMITS · ") + who; break;
    case 1: kind = QStringLiteral("penalty"); text = QStringLiteral(u"5 SEKUNDEN STRAFE · ") + who; break;
    case 2: kind = QStringLiteral("mom"); text = QStringLiteral(u"OVERTAKE MODE · ") + who; break;
    case 3: kind = QStringLiteral("penserved"); text = QStringLiteral(u"STRAFE ABGELEISTET · ") + who; break;
    default: kind = QStringLiteral("flag"); text = QStringLiteral(u"GRUENE FLAGGE · SEKTOR 2"); break;
    }
    QJsonObject m;
    m["id"] = m_rcId;
    m["type"] = kind;
    m["text"] = text;
    return QJsonArray{m};
}

void DemoFeed::emitPayload()
{
    const double t = m_clock.nsecsElapsed() / 1.0e9;
    const double lapTime = 92.0;
    const int lap = static_cast<int>(t / 8) + 1;            // eine "Runde" alle 8 Sekunden

    // Alle 70 s einmal durch die Zustaende: normal, VSC, Safety Car, rote Flagge.
    const int phase = static_cast<int>(t / 70) % 4;
    static const char* const phases[] = {"none", "vsc", "sc", "none"};
    const QString scStatus = QString::fromLatin1(phases[phase]);
    const bool redFlag = phase == 3 && std::fmod(t, 70.0) < 15;

    QJsonObject startLights;
    if (t < 6.0) {
        startLights["num"] = std::min(5, static_cast<int>(t));
        startLights["out"] = false;
        startLights["age"] = 0.2;
    } else if (t < 9.0) {
        startLights["num"] = 0;
        startLights["out"] = true;
        startLights["age"] = t - 6.0;
    } else {
        startLights["num"] = 0;
        startLights["out"] = false;
        startLights["age"] = 9999;
    }

    for (Driver& d : m_drivers) {
        if (t < d.pitUntil)
            continue;
        d.progress += 0.016 * d.pace * (scStatus != QLatin1String("none") ? 0.35 : 1.0);
        d.progress += std::sin(t * 0.7 + d.index) * 0.0009;

        // Boxenstopp: jeder Fahrer einmal um Runde 14 herum, gestaffelt.
        if (d.stops == 0 && lap >= 12 + (d.index % 7)) {
            d.stops = 1;
            d.pitUntil = t + 3.0;
            d.compound = QString::fromLatin1(kCompounds[(compoundIndex(d.compound) + 1) % 3]);
            d.tyreAge = 0;
        } else {
            d.tyreAge = std::min(45, (lap - d.stops * 12) + 2);
        }

        // Rundenzeit nur EINMAL pro Runde neu wuerfeln, sonst liefe der
        // Bestrunde-Flash im Dauerbetrieb.
        if (d.lastLapNum != lap) {
            d.lastLapNum = lap;
            d.last = lapTime / d.pace + uniform(-0.25, 0.25);
            if (d.best == 0 || d.last < d.best)
                d.best = d.last;
        }
    }

    QList<const Driver*> order;
    for (const Driver& d : m_drivers)
        order.append(&d);
    std::sort(order.begin(), order.end(), [](const Driver* a, const Driver* b) { return a->progress > b->progress; });
    const double leaderProg = order.first()->progress;
    const Driver* fastest = *std::min_element(order.begin(), order.end(), [](const Driver* a, const Driver* b) {
        return (a->best > 0 ? a->best : 9e9) < (b->best > 0 ? b->best : 9e9);
    });

    QJsonArray out;
    for (int pos = 1; pos <= order.size(); ++pos) {
        const Driver& d = *order.at(pos - 1);
        const double behind = leaderProg - d.progress;
        const double ahead = pos > 1 ? order.at(pos - 2)->progress - d.progress : 0.0;
        const bool inPit = t < d.pitUntil;
        const double s1 = 30.0 / d.pace + std::sin(t * 0.3 + d.index) * 0.15;
        const double s2 = 31.5 / d.pace + std::cos(t * 0.25 + d.index) * 0.15;
        const double wave = std::sin(d.progress * 6.3 + d.index);
        QJsonObject o;
        o["index"] = d.index;
        o["name"] = d.name;
        o["team"] = d.team;
        o["position"] = pos;
        o["gap_to_leader"] = round3(behind * 3.4);
        o["gap_to_ahead"] = round3(ahead * 3.4);
        o["compound"] = d.compound;
        o["tyre_age"] = d.tyreAge;
        o["last_lap"] = round3(d.last);
        o["best_lap"] = round3(d.best);
        o["current_lap_time"] = round3(std::fmod(t, 8.0) * 11.5);
        o["sector1"] = round3(s1);
        o["sector2"] = round3(s2);
        o["best_sectors"] = QJsonArray{round3(s1 - 0.2), round3(s2 - 0.2), 30.4};
        o["drs"] = pos > 1 && ahead * 3.4 < 1.0;
        o["overtake_active"] = (pos == 2 || pos == 5) && static_cast<int>(t) % 12 < 3;
        o["overtake_available"] = pos > 1 && ahead * 3.4 < 1.0;
        o["ers_pct"] = 40 + (d.index * 7) % 60;
        o["ers_mode"] = 1;
        o["speed"] = inPit ? 0 : static_cast<int>(150 + 130 * std::abs(wave));
        o["gear"] = inPit ? 0 : 3 + static_cast<int>(4 * std::abs(wave));
        o["throttle"] = inPit ? 0.0 : std::max(0.0, wave);
        o["brake"] = inPit ? 0.0 : std::max(0.0, -wave) * 0.9;
        o["pit_time"] = d.stops ? 2.4 + (d.index % 5) * 0.3 : 0.0;
        o["in_pit"] = inPit;
        o["dnf"] = d.index == 19 && lap > 6;
        o["dsq"] = false;
        o["driver_status"] = inPit ? 0 : 1;
        o["lap_invalid"] = m_quali && d.index % 9 == 3;
        o["penalties"] = (d.index == 3 && lap > 4) ? 5 : 0;
        o["pen_dt"] = (d.index == 8 && lap > 9) ? 1 : 0;
        o["corner_warnings"] = d.index == 11 ? (static_cast<int>(t / 20) % 3) : 0;
        o["lap_num"] = lap;
        o["lap_distance"] = std::fmod(d.progress, 1.0) * 5300;
        o["pos_xz"] = trackPoint(std::fmod(d.progress, 1.0));
        o["laps_down"] = (d.index == 17 && lap > 20) ? 1 : 0;
        o["sector"] = std::min(2, static_cast<int>(std::fmod(t, 8.0) / 2.7));
        o["grid_position"] = d.grid;
        o["dmg_fl"] = d.index == 5 ? 35 : 0;
        o["dmg_fr"] = 0;
        o["dmg_rw"] = (d.index == 2 && lap > 3) ? 72 : 0;
        o["finished"] = false;
        o["pit_stops"] = d.stops;
        o["stints"] = QJsonArray();
        out.append(o);
    }

    QJsonArray zones;
    if (scStatus != QLatin1String("none")) {
        const double starts[] = {0.0, 0.18, 0.34, 0.62, 0.78};
        const int flags[] = {0, 3, 0, 3, 0};
        for (int i = 0; i < 5; ++i)
            zones.append(QJsonObject{{"s", starts[i]}, {"f", flags[i]}});
    } else {
        zones.append(QJsonObject{{"s", 0.0}, {"f", 0}});
        zones.append(QJsonObject{{"s", 0.5}, {"f", 0}});
    }

    QJsonObject session;
    session["type"] = m_quali ? 5 : 10;
    session["type_name"] = m_quali ? QStringLiteral("Q1") : QStringLiteral("Rennen");
    session["is_quali"] = m_quali;
    session["current_lap"] = lap;
    session["total_laps"] = 58;
    session["track_length"] = 5300;
    session["track_id"] = 1;
    session["track_name"] = QStringLiteral("Paul Ricard");
    session["track_colors"] = QJsonArray{"#002395", "#FFFFFF", "#ED2939"};
    session["formula"] = 13;
    session["formula_name"] = QStringLiteral("F1 26");
    session["safety_car_status"] = scStatus;
    // Quali-Uhr laeuft in 220-s-Runden von 200 auf 0 (Gefahrenzone sichtbar).
    session["time_left"] = m_quali ? std::max(0, 200 - static_cast<int>(std::fmod(t, 220.0))) : 0;
    session["weather_name"] = QStringLiteral("Leicht bewoelkt");
    session["weather_emoji"] = QStringLiteral(u"⛅");
    session["weather_rain"] = 12;
    session["forecast"] = QJsonArray{
        QJsonObject{{"t", 5}, {"emoji", QStringLiteral(u"⛅")}, {"rain", 18}},
        QJsonObject{{"t", 10}, {"emoji", QStringLiteral(u"\U0001F327️")}, {"rain", 55}},
        QJsonObject{{"t", 15}, {"emoji", QStringLiteral(u"\U0001F327️")}, {"rain", 70}},
        QJsonObject{{"t", 30}, {"emoji", QStringLiteral(u"☀️")}, {"rain", 8}},
    };
    session["fastest_lap_driver"] = fastest->name;
    session["fastest_lap_time"] = round3(fastest->best);
    session["is_spectating"] = false;
    session["spectator_index"] = 255;
    session["marshal_zones"] = zones;

    // Erst die eigenen Vorgaben, darueber die echten Settings.
    QJsonObject settings{
        {"brand_title", QStringLiteral("KERS")}, {"tower", true}, {"ticker", true},
        {"damage", true}, {"comeback", true}, {"pbflash", true},
        {"opacity", 1.0}, {"scale", 0}, {"rows", 0},
    };
    for (auto it = m_settings.begin(); it != m_settings.end(); ++it)
        settings[it.key()] = it.value();

    QJsonObject regie;
    regie["chart"] = m_vorschau ? QStringLiteral("none") : demoChart(t);
    regie["champ"] = m_vorschau ? true : (std::fmod(t, 140.0) >= 100 && std::fmod(t, 140.0) < 125);
    regie["battles"] = true;
    regie["hotlap"] = true;

    QJsonObject data;
    data["drivers"] = out;
    data["connected"] = true;
    data["session"] = session;
    data["race_control"] = raceControl(t, redFlag, order);
    // Kamerawechsel alle 20 s -> das Lower-Third blendet neu ein.
    data["focus_index"] = order.at(static_cast<int>(t / 20) % std::min(6, static_cast<int>(order.size())))->index;
    data["start_lights"] = startLights;
    data["final_classification"] = QJsonArray();
    data["quali_results"] = QJsonObject();
    data["settings"] = settings;
    data["udp"] = QJsonObject{{"got", 2026}, {"want", 2026}};
    data["regie"] = regie;
    emit payload(data);
}

}  // namespace kers::hud
