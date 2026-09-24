#include "payload.h"

#include "constants.h"
#include "pyconv.h"
#include "jsonfile.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace kers::server {

QJsonObject buildLivePayload(const RaceState& st, const QJsonObject& settings, const QJsonObject& regie)
{
    const SessionInfo& s = st.session();
    const QString prefix = formulaName(s.formula);
    QList<Driver> active;
    for (const Driver& d : st.drivers()) {
        if (d.position > 0 && (st.validParticipants().isEmpty() || st.validParticipants().contains(d.index)))
            active.append(d);
    }
    // Reihenfolge kommt vom Spiel (m_carPosition) - NICHT selbst nach best_lap sortieren.
    std::stable_sort(active.begin(), active.end(), [](const Driver& a, const Driver& b) { return a.position < b.position; });

    // Ueberrundungen: Fortschritt = Runde + Rundenanteil. Aus dem KONTINUIERLICHEN
    // Fortschritt gerechnet -> kein Flackern an der Start/Ziel-Linie. Nur im Rennen.
    const int tl = s.track_length;
    if (tl > 0 && !s.is_quali && !active.isEmpty()) {
        auto prog = [tl](const Driver& e) { return e.lap_num + std::max(0.0, e.lap_distance) / tl; };
        double lead = 0.0;
        for (const Driver& e : active)
            lead = std::max(lead, prog(e));
        for (Driver& e : active)
            e.laps_down = std::max(0, static_cast<int>(lead - prog(e)));
    } else {
        for (Driver& e : active)
            e.laps_down = 0;
    }

    QJsonArray drivers;
    for (const Driver& d : active) {
        QJsonObject o = d.toJson();
        if (d.name.isEmpty()) {
            QString team = teamName(d.team_id);
            if (team.isEmpty())
                team = d.team;
            o["name"] = buildFallbackName(team, d.race_number, prefix);
        }
        const auto p = st.carPos().constFind(d.index);
        if (p != st.carPos().cend())
            o["pos_xz"] = QJsonArray{p->x(), p->y()};
        drivers.append(o);
    }

    // "Auf wen bin ich drauf": beim Zuschauen das beobachtete Auto, sonst das eigene.
    const int focus = (s.is_spectating && s.spectator_index != 255) ? s.spectator_index : st.playerIndex();
    const StartLights& sl = st.startLights();
    const double slAge = sl.t > 0 ? py::round(st.now() - sl.t, 2) : 9999;

    QJsonArray rc;
    for (const RaceControlMsg& m : st.raceControl())
        rc.append(QJsonObject{{"id", m.id}, {"type", m.type}, {"text", m.text}, {"lap", m.lap}, {"t", m.t}});
    QJsonArray fin;
    for (const QJsonObject& r : st.finalClassification())
        fin.append(r);
    QJsonObject quali;
    for (auto it = st.lastQuali().cbegin(); it != st.lastQuali().cend(); ++it)
        quali[it.key()] = it.value();

    QJsonObject out;
    out["drivers"] = drivers;
    out["connected"] = st.connected();
    out["session"] = s.toJson();
    out["race_control"] = rc;
    out["focus_index"] = focus;
    out["start_lights"] = QJsonObject{{"num", sl.num}, {"out", sl.out}, {"age", slAge}};
    out["final_classification"] = fin;
    out["quali_results"] = quali;
    out["settings"] = settings;
    out["udp"] = QJsonObject{{"got", st.rxFormat()}, {"want", EXPECTED_FORMAT}};
    out["regie"] = regie;
    return out;
}

QJsonObject calculateChampionship(const RaceState& st, const QString& champPath)
{
    // Gesamtpunkte = base_points + Punkte fuer die aktuelle Rennposition (Top 10).
    // Namensabgleich: in_game_name -> name -> Teilstring.
    bool ok = false;
    const QJsonObject cfg = json::readObject(champPath, &ok);
    if (!ok)
        return QJsonObject{{"title", ""}, {"standings", QJsonArray()}};
    // Nach Auto-Index geordnet, damit der Teilstring-Abgleich immer denselben
    // Treffer liefert (QHash hat keine feste Reihenfolge).
    QList<int> idx = st.drivers().keys();
    std::sort(idx.begin(), idx.end());
    QMap<QString, int> live;
    QList<QPair<QString, int>> liveOrdered;
    for (int i : idx) {
        const Driver& d = st.drivers()[i];
        const QString nm = d.name.trimmed();
        if (!nm.isEmpty() && d.position > 0) {
            if (!live.contains(nm))
                liveOrdered.append({nm, d.position});
            live[nm] = d.position;
        }
    }
    QList<QJsonObject> standings;
    for (const QJsonValue& v : cfg.value("drivers").toArray()) {
        const QJsonObject drv = v.toObject();
        const QString name = drv.value("name").toString();
        const double base = drv.value("base_points").toDouble();
        const QString ign = drv.value("in_game_name").toString().isEmpty() ? name : drv.value("in_game_name").toString();
        int pos = live.value(ign, 0);
        if (pos == 0)
            pos = live.value(name, 0);
        if (pos == 0 && !name.isEmpty()) {
            for (const auto& [teleName, telePos] : liveOrdered) {
                if (teleName.toLower().contains(name.toLower())) {
                    pos = live.value(teleName);
                    break;
                }
            }
        }
        const int livePts = championshipPoints(pos);
        QJsonObject sRow;
        sRow["name"] = name;
        sRow["team"] = drv.value("team").toString();
        sRow["base_points"] = base;
        sRow["live_points"] = livePts;
        sRow["total_points"] = base + livePts;
        if (pos > 0)
            sRow["live_position"] = pos;
        else
            sRow["live_position"] = QJsonValue::Null;
        standings.append(sRow);
    }
    std::stable_sort(standings.begin(), standings.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const double ta = a.value("total_points").toDouble(), tb = b.value("total_points").toDouble();
        if (ta != tb)
            return ta > tb;
        return a.value("base_points").toDouble() > b.value("base_points").toDouble();
    });
    QJsonArray arr;
    for (const QJsonObject& r : standings)
        arr.append(r);
    return QJsonObject{{"title", cfg.value("title").toString(QStringLiteral("Championship"))}, {"standings", arr}};
}

QJsonObject trackJson(RaceState& st)
{
    const TrackOutline& o = st.outline();
    TrackLearner& l = st.learner();
    const int tl = st.session().track_length;
    QJsonValue curProg = QJsonValue::Null;
    const auto ref = l.refIdx();
    if (ref && st.drivers().contains(*ref) && tl > 0) {
        const Driver& dd = st.drivers().value(*ref);
        curProg = py::round(dd.lap_num + std::max(0.0, dd.lap_distance) / tl, 3);
    }
    // Lernfortschritt 0..1 - wie weit ist das Referenzauto auf seiner Runde?
    QJsonValue lernt = QJsonValue::Null;
    if (!curProg.isNull() && l.startProg())
        lernt = py::round(std::clamp(curProg.toDouble() - *l.startProg(), 0.0, 1.0), 3);
    QJsonArray pts;
    for (const TrackPt& p : o.pts)
        pts.append(QJsonArray{p.x, p.z, p.frac, p.sector});
    QJsonObject dbg;
    dbg["ref_idx"] = ref ? QJsonValue(*ref) : QJsonValue::Null;
    dbg["start_prog"] = l.startProg() ? QJsonValue(*l.startProg()) : QJsonValue::Null;
    dbg["cur_prog"] = curProg;
    dbg["dist_m"] = static_cast<int>(std::lround(l.dist()));
    dbg["tl"] = tl;
    return QJsonObject{{"pts", pts}, {"done", o.done}, {"ver", o.ver}, {"lernt", lernt},
                       {"punkte", o.pts.size()}, {"_dbg", dbg}};
}

QJsonObject tracksJson(const RaceState& st)
{
    const SessionInfo& s = st.session();
    QSet<int> ids;
    const TrackStore& ts = st.tracks();
    for (auto it = ts.user().cbegin(); it != ts.user().cend(); ++it)
        ids.insert(it.key());
    for (auto it = ts.builtin().cbegin(); it != ts.builtin().cend(); ++it)
        ids.insert(it.key());
    QList<int> sorted(ids.begin(), ids.end());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray raus;
    for (int tid : sorted) {
        const bool eigen = ts.istEigen(tid);
        const TrackEntry* e = ts.eintrag(tid);
        const TrackInfo info = trackInfo(tid);
        QJsonObject r;
        r["id"] = tid;
        r["name"] = info.name;
        r["land"] = info.land;
        r["colors"] = QJsonArray::fromStringList(info.colors);
        r["len"] = e->len;
        r["pts"] = e->pts.size();
        r["rot"] = e->rot ? QJsonValue(*e->rot) : QJsonValue::Null;
        r["flip"] = e->flip ? QJsonValue(*e->flip) : QJsonValue::Null;
        r["quelle"] = eigen ? QStringLiteral("eigen") : QStringLiteral("mitgeliefert");
        r["aktiv"] = s.has_track_id && tid == s.track_id;
        raus.append(r);
    }
    QJsonObject out;
    out["tracks"] = raus;
    out["aktuell"] = s.has_track_id ? QJsonValue(s.track_id) : QJsonValue::Null;
    out["aktuell_name"] = s.has_track_id ? trackInfo(s.track_id).name : QString();
    return out;
}

QJsonObject lapPositionsJson(const RaceState& st)
{
    QJsonObject meta;
    for (const Driver& d : st.drivers())
        if (st.validParticipants().isEmpty() || st.validParticipants().contains(d.index))
            meta[QString::number(d.index)] = QJsonObject{{"name", d.name}, {"team", d.team}};
    QJsonObject laps;
    for (auto it = st.lapPositions().cbegin(); it != st.lapPositions().cend(); ++it) {
        QJsonObject row;
        for (auto c = it.value().cbegin(); c != it.value().cend(); ++c)
            row[QString::number(c.key())] = c.value();
        laps[QString::number(it.key())] = row;
    }
    return QJsonObject{{"laps", laps}, {"drivers", meta}};
}

}  // namespace kers::server
