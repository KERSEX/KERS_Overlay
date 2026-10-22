#include "race_state.h"

#include "constants.h"
#include "pyconv.h"
#include "packets.h"

#include <QDateTime>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace kers::server {

namespace {

double wallSec()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

double round1(double v) { return py::round(v, 1); }
double round2(double v) { return py::round(v, 2); }
double round3(double v) { return py::round(v, 3); }
double round4(double v) { return py::round(v, 4); }

}  // namespace

// ── JSON ─────────────────────────────────────────────────────────────────────
QJsonObject Driver::toJson() const
{
    QJsonObject o;
    o["index"] = index;
    o["name"] = name;
    o["team"] = team;
    o["team_id"] = team_id;
    o["position"] = position;
    o["gap_to_leader"] = gap_to_leader;
    o["gap_to_ahead"] = gap_to_ahead;
    o["compound"] = compound;
    o["tyre_age"] = tyre_age;
    o["last_lap"] = last_lap;
    o["current_lap_time"] = current_lap_time;
    o["sector1"] = sector1;
    o["sector2"] = sector2;
    o["drs"] = drs;
    o["ers_pct"] = ers_pct;
    o["ers_mode"] = ers_mode;
    o["in_pit"] = in_pit;
    o["dnf"] = dnf;
    o["dsq"] = dsq;
    o["finished"] = finished;
    o["race_number"] = race_number;
    o["best_lap"] = best_lap;
    o["driver_status"] = driver_status;
    o["pit_time"] = pit_time;
    o["overtake_active"] = overtake_active;
    o["overtake_available"] = overtake_available;
    o["lap_invalid"] = lap_invalid;
    o["penalties"] = penalties;
    o["pen_dt"] = pen_dt;
    o["corner_warnings"] = corner_warnings;
    o["pit_stops"] = pit_stops;
    o["stints"] = QJsonArray::fromStringList(stints);
    o["lap_num"] = lap_num;
    o["lap_distance"] = lap_distance;
    o["laps_down"] = laps_down;
    o["sector"] = sector;
    o["grid_position"] = grid_position;
    o["speed"] = speed;
    o["throttle"] = throttle;
    o["brake"] = brake;
    o["gear"] = gear;
    o["dmg_fl"] = dmg_fl;
    o["dmg_fr"] = dmg_fr;
    o["dmg_rw"] = dmg_rw;
    o["tyre_wear"] = tyre_wear;
    o["best_sectors"] = QJsonArray{best_sectors[0], best_sectors[1], best_sectors[2]};
    o["best_lap_sectors"] = QJsonArray{best_lap_sectors[0], best_lap_sectors[1], best_lap_sectors[2]};
    o["name_hidden"] = name_hidden;
    return o;
}

QJsonObject SessionInfo::toJson() const
{
    QJsonObject o;
    o["type"] = type;
    o["type_name"] = type_name;
    if (has_track_id) {
        o["track_id"] = track_id;
        o["track_name"] = track_name;
        o["track_land"] = track_land;
        o["track_colors"] = QJsonArray::fromStringList(track_colors);
        o["track_known"] = track_known;
    }
    o["track_length"] = track_length;
    o["weather"] = weather;
    o["weather_name"] = weather_name;
    o["weather_emoji"] = weather_emoji;
    o["weather_rain"] = weather_rain;
    o["track_temp"] = track_temp;
    o["air_temp"] = air_temp;
    o["total_laps"] = total_laps;
    o["current_lap"] = current_lap;
    o["fastest_lap_time"] = fastest_lap_time;
    o["fastest_lap_driver"] = fastest_lap_driver;
    o["is_quali"] = is_quali;
    o["time_left"] = time_left;
    o["formula"] = formula;
    o["formula_name"] = formula_name;
    o["safety_car"] = safety_car;
    o["safety_car_status"] = safety_car_status;
    o["is_spectating"] = is_spectating;
    o["spectator_index"] = spectator_index;
    o["marshal_zones"] = marshal_zones;
    o["forecast"] = forecast;
    return o;
}

// ── Namen ────────────────────────────────────────────────────────────────────
bool isPlaceholderName(const QString& name)
{
    QString n = name.trimmed().toLower();
    if (n.isEmpty())
        return true;
    // nachgestellte Nummer/Leerzeichen entfernen ("Player 1" -> "player")
    while (!n.isEmpty() && (n.back().isDigit() || n.back() == QLatin1Char(' ')))
        n.chop(1);
    n = n.trimmed();
    static const QStringList ph = {"player", "spieler", "spielerin", "spieler:in", "spieler in"};
    return ph.contains(n);
}

QString buildFallbackName(const QString& team, int raceNumber, const QString& formulaPrefix)
{
    if (!team.isEmpty() && raceNumber > 0)
        return QStringLiteral("%1 #%2").arg(team).arg(raceNumber);
    if (!team.isEmpty())
        return team;
    if (raceNumber > 0)
        return QStringLiteral("%1 #%2").arg(formulaPrefix).arg(raceNumber);
    return QStringLiteral("%1 #?").arg(formulaPrefix);
}

// ── RaceState ────────────────────────────────────────────────────────────────
RaceState::RaceState(SettingsStore& settings, TrackStore& tracks)
    : m_settings(settings)
    , m_tracks(tracks)
    , m_clock(&wallSec)
    , m_learner(tracks, m_outline)
{
    // Ausrichtung je Strecke: einmal zurechtgedreht, ab dann richtig.
    m_tracks.setAusrichtungAnwenden([this](const TrackEntry& e) {
        if (e.rot)
            m_settings.set(QStringLiteral("maprot"), *e.rot);
        if (e.flip)
            m_settings.set(QStringLiteral("mapflip"), *e.flip);
    });
}

bool RaceState::connected() const
{
    return (now() - m_lastPacketTime) < CONNECTED_S;
}

Driver& RaceState::driver(int idx)
{
    auto it = m_drivers.find(idx);
    if (it == m_drivers.end()) {
        Driver d;
        d.index = idx;
        it = m_drivers.insert(idx, d);
    }
    return it.value();
}

QString RaceState::carName(int idx) const
{
    const auto it = m_drivers.constFind(idx);
    if (it != m_drivers.cend() && !it->name.isEmpty())
        return it->name;
    return QStringLiteral("Auto #%1").arg(idx);
}

void RaceState::pushRc(const QString& type, const QString& text)
{
    ++m_rcId;
    m_raceControl.append(RaceControlMsg{m_rcId, type, text, m_session.current_lap, now()});
    if (m_raceControl.size() > RC_KEEP)
        m_raceControl.removeFirst();
}

QList<LearnerCar> RaceState::learnerCars() const
{
    QList<LearnerCar> out;
    for (const Driver& d : m_drivers)
        out.append(LearnerCar{d.index, d.position, d.in_pit, d.dnf, d.dsq, d.lap_invalid, d.lap_num, d.lap_distance, d.sector});
    return out;
}

void RaceState::sessionWechsel()
{
    // War die alte Session eine Quali? -> Bestzeiten fuers Grid-Screen retten.
    if (m_session.is_quali) {
        m_lastQuali.clear();
        for (const Driver& d : m_drivers)
            if (d.best_lap > 0 && !d.name.isEmpty())
                m_lastQuali[d.name] = d.best_lap;
    }
    m_drivers.clear();
    m_lapTrack.clear();
    m_valid.clear();
    m_session.current_lap = 0;
    m_session.fastest_lap_time = 0.0;
    m_session.fastest_lap_driver.clear();
    m_raceControl.clear();
    m_carPos.clear();
    m_final.clear();
    m_lapPositions.clear();
    m_lights = StartLights{};
    // Neue Session -> Trackmap neu lernen (nicht die alte Kontur zeigen).
    m_learner.sessionNeu();
}

int RaceState::feed(const QByteArray& d)
{
    const PacketHeader h = parseHeader(d);
    if (!h.ok)
        return -1;
    m_rxFormat = h.format;
    if (h.format != EXPECTED_FORMAT && h.format != m_warnedFormat) {
        qWarning() << QStringLiteral("[UDP] Format %1 empfangen, erwartet %2 - im Spiel UDP-Format auf '2026' stellen").arg(h.format).arg(EXPECTED_FORMAT);
        m_warnedFormat = h.format;
    }
    if (!m_haveUid || h.sessionUid != m_lastUid) {
        if (m_haveUid)
            sessionWechsel();
        else
            m_learner.sessionNeu();
        m_haveUid = true;
        m_lastUid = h.sessionUid;
    }
    m_lastPacketTime = now();
    m_playerIdx = h.playerIndex;
    switch (h.packetId) {
    case 0: handleMotion(d); break;
    case 1: handleSession(d); break;
    case 2: handleLapData(d); break;
    case 3: handleEvent(d); break;
    case 4: handleParticipants(d); break;
    case 6: handleCarTelemetry(d); break;
    case 7: handleCarStatus(d); break;
    case 8: handleFinalClassification(d); break;
    case 10: handleCarDamage(d); break;
    case 11: handleSessionHistory(d); break;
    case 15: handleLapPositions(d); break;
    case 16: handleCarTelemetry2(d); break;
    default: break;
    }
    return h.packetId;
}

// ── PID 1: Session ───────────────────────────────────────────────────────────
void RaceState::handleSession(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    if (d.size() < base + 125)
        return;
    const int weather = u8(d, base + 0);
    const int trackTemp = u8(d, base + 1);
    const int airTemp = u8(d, base + 2);
    const int totalLaps = u8(d, base + 3);
    const int trackLength = u16(d, base + 4);             // m_trackLength (Meter)
    const int sessionType = u8(d, base + 6);
    const int trackId = i8(d, base + 7);                  // int8 - signed!
    const int formula = u8(d, base + 8);
    const int tlRaw = u16(d, base + 9);
    const int timeLeft = tlRaw <= 3600 ? tlRaw : 0;
    // m_safetyCarStatus: 19 feste Bytes + 21 MarshalZones a 5 Bytes = Offset 124.
    const int safetyCar = u8(d, base + 124);
    const bool isSpectating = u8(d, base + 15) != 0;
    const int spectatorIdx = u8(d, base + 16);

    // Marshal-Zonen: Anzahl @ +18, danach 21 Zonen a 5 Bytes (float zoneStart, int8 zoneFlag).
    QJsonArray zones;
    const int numMz = u8(d, base + 18);
    for (int k = 0; k < std::min(numMz, 21); ++k) {
        const int off = base + 19 + k * 5;
        if (d.size() < off + 5)
            break;
        const float zs = f32(d, off);
        const int zf = i8(d, off + 4);
        if (zs >= 0.0f && zs <= 1.0f)
            zones.append(QJsonObject{{"s", round4(zs)}, {"f", zf}});
    }

    // Wetter-Vorhersage: Anzahl @ +126, danach 8-Byte-Samples
    // (sessionType@0, timeOffset@1, weather@2, rainPercentage@7).
    QJsonArray forecast;
    int currentRain = 0;
    if (d.size() >= base + 127) {
        const int numFc = u8(d, base + 126);
        for (int i = 0; i < std::min(numFc, 56); ++i) {
            const int off = base + 127 + i * 8;
            if (d.size() < off + 8)
                break;
            const int fcSession = u8(d, off + 0);
            const int timeOffset = u8(d, off + 1);
            const int fcWeather = u8(d, off + 2);
            const int rain = u8(d, off + 7);
            if (fcSession != sessionType)
                continue;
            if (timeOffset == 0) {
                currentRain = rain;
                continue;
            }
            forecast.append(QJsonObject{{"t", timeOffset}, {"weather", fcWeather}, {"emoji", weatherEmoji(fcWeather)}, {"rain", rain}});
            if (forecast.size() >= 5)
                break;
        }
    }

    // Streckenwechsel -> Kontur neu bestimmen. Greift auch beim allerersten Paket.
    if (!m_session.has_track_id || m_session.track_id != trackId)
        m_learner.streckeGewechselt(trackId, trackLength);

    const TrackInfo ti = trackInfo(trackId);
    m_session.has_track_id = true;
    m_session.track_id = trackId;
    m_session.track_name = ti.name;
    m_session.track_land = ti.land;
    m_session.track_colors = ti.colors;
    m_session.track_known = m_tracks.eintrag(trackId) != nullptr;
    m_session.track_length = trackLength;
    m_session.type = sessionType;
    m_session.type_name = sessionTypeName(sessionType);
    m_session.weather = weather;
    m_session.weather_name = weatherName(weather);
    m_session.weather_emoji = weatherEmoji(weather);
    m_session.weather_rain = currentRain;
    m_session.track_temp = trackTemp;
    m_session.air_temp = airTemp;
    m_session.total_laps = totalLaps;
    m_session.is_quali = isQualiType(sessionType);
    m_session.time_left = timeLeft;
    m_session.formula = formula;
    m_session.formula_name = formulaName(formula);
    m_session.safety_car = safetyCar;
    m_session.safety_car_status = safetyCarStatus(safetyCar);
    m_session.is_spectating = isSpectating;
    m_session.spectator_index = spectatorIdx;
    m_session.marshal_zones = zones;
    m_session.forecast = forecast;
}

// ── PID 4: Participants ──────────────────────────────────────────────────────
void RaceState::handleParticipants(const QByteArray& d)
{
    const int base = HEADER_SIZE + 1;                      // m_numActiveCars ueberspringen
    const QString prefix = formulaName(m_session.formula);
    m_valid.clear();
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * PARTICIPANT_SIZE;
        if (start + PARTICIPANT_SIZE > d.size())
            break;
        const int teamId = u16(d, start + 5);              // uint16 in F1 26
        if (teamId == 255 || teamId == 65535) {            // unbelegter Slot
            m_drivers.remove(i);
            continue;
        }
        m_valid.insert(i);
        const int raceNumber = u8(d, start + 8);
        QByteArray nameBytes = d.mid(start + 10, 32);
        const int nul = nameBytes.indexOf('\0');
        if (nul >= 0)
            nameBytes.truncate(nul);
        QString name = QString::fromUtf8(nameBytes).trimmed();
        const QString tName = teamName(teamId);
        const QString display = tName.isEmpty() ? QStringLiteral("T%1").arg(teamId) : tName;
        const bool hidden = isPlaceholderName(name);
        if (hidden)
            name = buildFallbackName(tName, raceNumber, prefix);
        Driver& dr = driver(i);
        dr.name = name;
        dr.team = display;
        dr.team_id = teamId;
        dr.race_number = raceNumber;
        dr.name_hidden = hidden;
    }
}

// ── PID 2: Lap Data ──────────────────────────────────────────────────────────
void RaceState::handleLapData(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    int maxLap = 0;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * LAP_DATA_SIZE;
        if (start + LAP_DATA_SIZE > d.size())
            break;
        const uint32_t lastLapMs = u32(d, start + 0);
        const uint32_t curLapMs = u32(d, start + 4);
        const int s1Ms = u16(d, start + 8), s1Min = u8(d, start + 10);
        const int s2Ms = u16(d, start + 11), s2Min = u8(d, start + 13);
        const int gapFront = u16(d, start + 14), gapFrontM = u8(d, start + 16);
        const int gapLeader = u16(d, start + 17), gapLeaderM = u8(d, start + 19);
        const float lapDistance = f32(d, start + 20);
        const int position = u8(d, start + 32);
        const int lapNumNow = u8(d, start + 33);
        const int pitStatus = u8(d, start + 34);
        const int numPitStops = u8(d, start + 35);
        const int curSector = u8(d, start + 36);
        const int lapInvalid = u8(d, start + 37);
        const int penaltiesS = u8(d, start + 38);
        const int cornerWarns = u8(d, start + 40);
        const int dtPens = u8(d, start + 41);
        const int sgPens = u8(d, start + 42);
        const int gridPos = u8(d, start + 43);
        const int driverStatus = u8(d, start + 44);
        const int resultStat = u8(d, start + 45);
        const int pitTimeMs = u16(d, start + 49);

        // Leere Slots: Position 0, result_status 0 oder kein echter Teilnehmer.
        if (position == 0 || resultStat == 0)
            continue;
        if (!m_valid.isEmpty() && !m_valid.contains(i))
            continue;
        Driver& dr = driver(i);
        // Gueltigkeit der Runde: m_currentLapInvalid gilt fuer die LAUFENDE Runde
        // -> ueber die Runde sammeln ("sticky"), beim Rundenwechsel festhalten.
        LapTrack& lt = m_lapTrack[i];
        if (lapNumNow > dr.lap_num) {
            lt.valid = !lt.dirty;
            lt.dirty = false;
        }
        if (lapInvalid == 1)
            lt.dirty = true;

        if (lastLapMs > 30000 && lastLapMs < 600000) {
            const double lapS = lastLapMs / 1000.0;
            dr.last_lap = lapS;
            // Ungueltige Runden (Track Limits) zaehlen NICHT als Best-/schnellste Runde.
            if (lt.valid) {
                if ((m_session.fastest_lap_time == 0.0 || lapS < m_session.fastest_lap_time) && !dr.name.isEmpty()) {
                    m_session.fastest_lap_time = lapS;
                    m_session.fastest_lap_driver = dr.name;
                }
                if (dr.best_lap == 0.0 || lapS < dr.best_lap)
                    dr.best_lap = lapS;
            }
        }
        dr.current_lap_time = (curLapMs > 0 && curLapMs < 600000) ? curLapMs / 1000.0 : 0.0;
        const double s1 = s1Min * 60 + s1Ms / 1000.0;
        const double s2 = s2Min * 60 + s2Ms / 1000.0;
        dr.sector1 = (s1 > 0 && s1 < 300) ? s1 : 0.0;
        dr.sector2 = (s2 > 0 && s2 < 300) ? s2 : 0.0;
        // Gap: Minuten-Teil nur wenn < 5 (mehr ist unmoeglich)
        const int aheadMin = gapFrontM < 5 ? gapFrontM : 0;
        const int leaderMin = gapLeaderM < 5 ? gapLeaderM : 0;
        dr.gap_to_ahead = aheadMin * 60.0 + gapFront / 1000.0;
        dr.gap_to_leader = leaderMin * 60.0 + gapLeader / 1000.0;
        dr.position = position;
        dr.in_pit = pitStatus == 1 || pitStatus == 2;
        dr.pit_stops = numPitStops;
        dr.lap_invalid = lapInvalid == 1;
        // Offene Stop-&-Go zaehlen mit je STOPGO_SEK Sekunden mit - das Spiel fuehrt
        // sie NICHT in m_penalties, sondern nur als Anzahl in einem eigenen Byte.
        // ⚠ Hier und nicht in den Anzeigen: Tower, Web-Overlay und Regie lesen alle
        // dieses eine Feld.
        dr.penalties = penaltiesS + STOPGO_SEK * sgPens;
        dr.pen_dt = dtPens;
        dr.corner_warnings = cornerWarns;
        dr.dnf = resultStat == 4 || resultStat == 6 || resultStat == 7;
        dr.dsq = resultStat == 5;
        dr.finished = resultStat == 3;
        dr.driver_status = driverStatus;
        dr.lap_distance = lapDistance;
        dr.sector = (curSector >= 0 && curSector <= 2) ? curSector : 0;
        dr.grid_position = gridPos;
        if (pitTimeMs > 0)
            dr.pit_time = pitTimeMs / 1000.0;          // sticky
        // Fallback-Name falls noch keiner gesetzt (Lap-Daten vor Participants-Paket)
        if (dr.name.isEmpty() && position > 0) {
            QString team = teamName(dr.team_id);
            if (team.isEmpty())
                team = dr.team;
            dr.name = buildFallbackName(team, dr.race_number, formulaName(m_session.formula));
        }
        if (position > 0) {
            dr.lap_num = lapNumNow;
            maxLap = std::max(maxLap, lapNumNow);
        }
    }
    // current_lap = Runde des Fuehrenden, jeden Frame neu gesetzt (nicht monoton).
    if (maxLap > 0)
        m_session.current_lap = maxLap;
}

// ── PID 7: Car Status ────────────────────────────────────────────────────────
void RaceState::handleCarStatus(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * CAR_STATUS_SIZE;
        if (start + CAR_STATUS_SIZE > d.size())
            break;
        const int visual = u8(d, start + 26);
        const int tyreAge = u8(d, start + 27);
        const float ersStore = f32(d, start + 37);
        const int ersMode = u8(d, start + 41);
        Driver& dr = driver(i);
        dr.compound = compoundLetter(visual);
        dr.tyre_age = tyreAge;
        dr.ers_pct = round1(std::clamp(static_cast<double>(ersStore) / ERS_MAX * 100.0, 0.0, 100.0));
        dr.ers_mode = ersMode;
        // Stint-Historie: bei (gueltigem) Compound-Wechsel anhaengen
        static const QString erlaubt = QStringLiteral("SMHIWE");
        if (dr.compound.size() == 1 && erlaubt.contains(dr.compound)) {
            if (dr.stints.isEmpty() || dr.stints.last() != dr.compound) {
                dr.stints.append(dr.compound);
                if (dr.stints.size() > 6)
                    dr.stints.removeFirst();
            }
        }
    }
}

// ── PID 6: Car Telemetry ─────────────────────────────────────────────────────
void RaceState::handleCarTelemetry(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * CAR_TELEM_SIZE;
        if (start + 19 > d.size())
            break;
        Driver& dr = driver(i);
        dr.speed = u16(d, start + 0);
        dr.throttle = round2(std::clamp(static_cast<double>(f32(d, start + 2)), 0.0, 1.0));
        dr.brake = round2(std::clamp(static_cast<double>(f32(d, start + 10)), 0.0, 1.0));
        dr.gear = i8(d, start + 15);
        dr.drs = u8(d, start + 18) == 1;
    }
}

// ── PID 16: Car Telemetry 2 (2026: Overtake Mode) ────────────────────────────
void RaceState::handleCarTelemetry2(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * CAR_TELEM2_SIZE;
        if (start + CAR_TELEM2_SIZE > d.size())
            break;
        Driver& dr = driver(i);
        dr.overtake_available = u8(d, start + 4) == 1;
        dr.overtake_active = u8(d, start + 5) == 1;
    }
}

// ── PID 0: Motion ────────────────────────────────────────────────────────────
void RaceState::handleMotion(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * MOTION_SIZE;
        if (start + 12 > d.size())
            break;
        const float x = f32(d, start + 0);
        const float z = f32(d, start + 8);
        // Leere Slots senden Muell (±1e38, NaN) -> verwerfen.
        if (x > -1e5f && x < 1e5f && z > -1e5f && z < 1e5f)
            m_carPos[i] = QPointF(round1(x), round1(z));
    }
    m_learner.learn(m_carPos, learnerCars(), m_session.track_length, m_session.track_id);
}

// ── PID 8: Final Classification ──────────────────────────────────────────────
void RaceState::handleFinalClassification(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    if (d.size() < base + 1)
        return;
    const int num = u8(d, base);
    QList<QJsonObject> results;
    for (int i = 0; i < std::min(num, MAX_CARS); ++i) {
        const int start = base + 1 + i * FINAL_CLASS_SIZE;
        if (start + FINAL_CLASS_SIZE > d.size())
            break;
        const int pos = u8(d, start), laps = u8(d, start + 1), grid = u8(d, start + 2);
        const int points = u8(d, start + 3), stops = u8(d, start + 4), status = u8(d, start + 5);
        if (status == 0 || status == 1 || pos == 0)
            continue;
        const uint32_t bestMs = u32(d, start + 7);
        const double totalS = f64(d, start + 11);           // double!
        const int penS = u8(d, start + 19);
        const int nStints = u8(d, start + 21);
        QJsonArray stints;
        for (int k = 0; k < std::min(nStints, 8); ++k)
            stints.append(compoundLetter(u8(d, start + 30 + k)));   // visual
        const auto dr = m_drivers.constFind(i);
        QJsonObject r;
        r["position"] = pos;
        r["index"] = i;
        r["name"] = (dr != m_drivers.cend() && !dr->name.isEmpty()) ? dr->name : QStringLiteral("Auto #%1").arg(i);
        r["team"] = dr != m_drivers.cend() ? dr->team : QString();
        r["grid"] = grid;
        r["points"] = points;
        r["stops"] = stops;
        r["laps"] = laps;
        r["status"] = status;
        r["best_lap"] = bestMs / 1000.0;
        r["total_time"] = totalS + penS;
        r["penalties"] = penS;
        r["stints"] = stints;
        results.append(r);
    }
    if (!results.isEmpty()) {
        std::sort(results.begin(), results.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("position").toInt() < b.value("position").toInt();
        });
        m_final = results;
    }
}

// ── PID 10: Car Damage ───────────────────────────────────────────────────────
void RaceState::handleCarDamage(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    for (int i = 0; i < MAX_CARS; ++i) {
        const int start = base + i * DAMAGE_SIZE;
        if (start + DAMAGE_SIZE > d.size())
            break;
        Driver& dr = driver(i);
        dr.dmg_fl = u8(d, start + 28);
        dr.dmg_fr = u8(d, start + 29);
        dr.dmg_rw = u8(d, start + 30);
        double wear = 0.0;
        for (int k = 0; k < 4; ++k)
            wear += f32(d, start + k * 4);
        dr.tyre_wear = static_cast<int>(std::nearbyint(wear / 4.0));   // round() wie Python
    }
}

// ── PID 15: Lap Positions ────────────────────────────────────────────────────
void RaceState::handleLapPositions(const QByteArray& d)
{
    const int base = HEADER_SIZE;
    if (d.size() < base + 2)
        return;
    const int numLaps = u8(d, base);
    const int lapStart = u8(d, base + 1);
    const int grid = base + 2;
    for (int L = 0; L < std::min(numLaps, 50); ++L) {
        const int row = grid + L * MAX_CARS;
        if (row + MAX_CARS > d.size())
            break;
        QMap<int, int> entry;
        for (int c = 0; c < MAX_CARS; ++c) {
            const int p = u8(d, row + c);
            if (p > 0)
                entry[c] = p;
        }
        if (!entry.isEmpty())
            m_lapPositions[lapStart + L + 1] = entry;       // 1-basierte Rundennummer
    }
}

// ── PID 11: Session History ──────────────────────────────────────────────────
void RaceState::handleSessionHistory(const QByteArray& d)
{
    // Die ECHTE Bestzeit eines Fahrers (rotierend, ein Auto pro Frame) - auch von
    // Runden VOR dem Start des Overlays.
    const int base = HEADER_SIZE;
    if (d.size() < base + 7)
        return;
    const int carIdx = u8(d, base);
    if (carIdx >= MAX_CARS)
        return;
    auto lapOff = [&](int lapNum) -> int {
        if (lapNum < 1 || lapNum > 100)
            return -1;
        const int o = base + 7 + (lapNum - 1) * LAP_HISTORY_SIZE;
        return o + LAP_HISTORY_SIZE <= d.size() ? o : -1;
    };
    Driver& dr = driver(carIdx);

    static const int secOff[3][2] = {{4, 6}, {7, 9}, {10, 12}};
    const int off = lapOff(u8(d, base + 3));                // m_bestLapTimeLapNum
    if (off >= 0) {
        const uint32_t ms = u32(d, off);
        if (ms > 30000 && ms < 600000) {
            const double lapS = ms / 1000.0;
            dr.best_lap = lapS;                              // autoritativ
            if ((m_session.fastest_lap_time == 0.0 || lapS < m_session.fastest_lap_time) && !dr.name.isEmpty()) {
                m_session.fastest_lap_time = lapS;
                m_session.fastest_lap_driver = dr.name;
            }
            // Die Sektoren DIESER Runde - daraus rechnen die Hotlap-Karten die
            // Zwischenzeit, gegen die gerade gefahren wird (F1-TV-Zielzeit).
            for (int k = 0; k < 3; ++k) {
                const double t = u8(d, off + secOff[k][1]) * 60 + u16(d, off + secOff[k][0]) / 1000.0;
                dr.best_lap_sectors[k] = (t > 0 && t < 300) ? t : 0.0;
            }
        }
    }
    // Bestsektoren: jeder Sektor kann in einer ANDEREN Runde gefallen sein.
    std::array<double, 3> secs{0, 0, 0};
    bool any = false;
    for (int k = 0; k < 3; ++k) {
        const int so = lapOff(u8(d, base + 4 + k));
        if (so < 0)
            continue;
        const int msPart = u16(d, so + secOff[k][0]);
        const int mnPart = u8(d, so + secOff[k][1]);
        const double t = mnPart * 60 + msPart / 1000.0;
        secs[k] = (t > 0 && t < 300) ? t : 0.0;
        if (secs[k] > 0)
            any = true;
    }
    if (any)
        dr.best_sectors = secs;
}

// ── PID 3: Event ─────────────────────────────────────────────────────────────
void RaceState::handleEvent(const QByteArray& d)
{
    if (d.size() < HEADER_SIZE + 4)
        return;
    const QString code = QString::fromLatin1(d.mid(HEADER_SIZE, 4));
    const int b = HEADER_SIZE + 4;
    if (code == QLatin1String("PENA") && d.size() >= HEADER_SIZE + 11) {
        const int penType = u8(d, b), veh = u8(d, b + 2), penTime = u8(d, b + 4);
        const QString name = carName(veh);
        if (penType >= 10 && penType <= 14) {                // Runde(n) annulliert = Track Limits
            pushRc(QStringLiteral("tracklimit"), QStringLiteral("%1: Track Limits").arg(name));
        } else {
            QString label = penaltyLabel(penType);
            if (penType == 4 && penTime > 0)
                label += QStringLiteral(" +%1s").arg(penTime);
            pushRc(QStringLiteral("penalty"), QStringLiteral("%1: %2").arg(name, label));
        }
    } else if (code == QLatin1String("DRSE")) {
        pushRc(QStringLiteral("mom"), m_session.formula == 13 ? QStringLiteral("MOM / OVERRIDE AKTIV") : QStringLiteral("DRS AKTIV"));
    } else if (code == QLatin1String("DRSD")) {
        pushRc(QStringLiteral("mom"), m_session.formula == 13 ? QStringLiteral("MOM / OVERRIDE AUS") : QStringLiteral("DRS AUS"));
    } else if (code == QLatin1String("FTLP") && d.size() >= HEADER_SIZE + 9) {
        const int veh = u8(d, b);
        const float lapTime = f32(d, b + 1);
        const QString name = carName(veh);
        if (lapTime > 0 && lapTime < 600) {
            m_session.fastest_lap_time = round3(lapTime);
            m_session.fastest_lap_driver = name;
            const int mm = static_cast<int>(lapTime / 60);
            const double ss = std::fmod(lapTime, 60.0f);
            pushRc(QStringLiteral("fl"), QStringLiteral("%1: Schnellste Runde %2:%3").arg(name).arg(mm).arg(ss, 6, 'f', 3, QLatin1Char('0')));
        }
    } else if (code == QLatin1String("RTMT") && d.size() >= HEADER_SIZE + 5) {
        pushRc(QStringLiteral("retire"), QStringLiteral("%1: Aufgabe (DNF)").arg(carName(u8(d, b))));
    } else if (code == QLatin1String("RDFL")) {
        pushRc(QStringLiteral("redflag"), QStringLiteral("ROTE FLAGGE"));
    } else if (code == QLatin1String("CHQF")) {
        pushRc(QStringLiteral("flag"), QStringLiteral("Zielflagge"));
    } else if (code == QLatin1String("RCWN") && d.size() >= HEADER_SIZE + 5) {
        pushRc(QStringLiteral("winner"), QStringLiteral("%1 gewinnt das Rennen!").arg(carName(u8(d, b))));
    } else if (code == QLatin1String("DTSV") && d.size() >= HEADER_SIZE + 5) {
        pushRc(QStringLiteral("penserved"), QStringLiteral("%1: Drive Through Penalty abgeleistet").arg(carName(u8(d, b))));
    } else if (code == QLatin1String("SGSV") && d.size() >= HEADER_SIZE + 5) {
        pushRc(QStringLiteral("penserved"), QStringLiteral("%1: %2 Sek Strafe abgeleistet").arg(carName(u8(d, b))).arg(STOPGO_SEK));
    } else if (code == QLatin1String("STLG") && d.size() >= HEADER_SIZE + 5) {
        m_lights.num = u8(d, b);
        m_lights.out = false;
        m_lights.t = now();
    } else if (code == QLatin1String("LGOT")) {
        m_lights.out = true;
        m_lights.t = now();
    } else if (code == QLatin1String("SSTA")) {
        m_lights = StartLights{};
        m_final.clear();
    }
}

}  // namespace kers::server
