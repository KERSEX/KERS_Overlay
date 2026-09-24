// Der Rennzustand des Servers - Portierung der Telemetrie-Container und
// handle_*-Funktionen aus main.py.
//
// Alles laeuft im Haupt-Thread (QUdpSocket::readyRead), deshalb ohne Sperren.
// Die Felder heissen wie die JSON-Schluessel des Payloads (snake_case), weil
// sie genau dort landen - und drei Clients (HUD, Web-Overlay, Regie) daran haengen.
#pragma once

#include "settings_store.h"
#include "tracks.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <functional>

namespace kers::server {

struct Driver {
    int index = -1;
    QString name, team;
    int team_id = -1;
    int position = 0;
    double gap_to_leader = 0, gap_to_ahead = 0;
    QString compound = QStringLiteral("?");
    int tyre_age = 0;
    double last_lap = 0, current_lap_time = 0, sector1 = 0, sector2 = 0;
    bool drs = false;
    double ers_pct = 0;
    int ers_mode = 0;
    bool in_pit = false, dnf = false, dsq = false, finished = false;
    int race_number = 0;
    double best_lap = 0;
    int driver_status = 0;
    double pit_time = 0;
    bool overtake_active = false, overtake_available = false;
    bool lap_invalid = false;
    int penalties = 0, pen_dt = 0, corner_warnings = 0, pit_stops = 0;
    QStringList stints;
    int lap_num = 0;
    double lap_distance = 0;
    int laps_down = 0;
    int sector = 0, grid_position = 0;
    int speed = 0;
    double throttle = 0, brake = 0;
    int gear = 0;
    int dmg_fl = 0, dmg_fr = 0, dmg_rw = 0, tyre_wear = 0;
    std::array<double, 3> best_sectors{0, 0, 0};
    bool name_hidden = false;

    QJsonObject toJson() const;
};

struct SessionInfo {
    int type = 0;
    QString type_name = QStringLiteral("Unbekannt");
    bool has_track_id = false;
    int track_id = -1;
    QString track_name, track_land;
    QStringList track_colors;
    bool track_known = false;
    int track_length = 0;
    int weather = 0;
    QString weather_name = QStringLiteral(u"Klar ☀️");
    QString weather_emoji = QStringLiteral(u"☀️");
    int weather_rain = 0;
    int track_temp = 0, air_temp = 0;
    int total_laps = 0, current_laps_unused = 0;
    int current_lap = 0;
    double fastest_lap_time = 0;
    QString fastest_lap_driver;
    bool is_quali = false;
    int time_left = 0;
    int formula = 0;
    QString formula_name = QStringLiteral("F1");
    int safety_car = 0;
    QString safety_car_status = QStringLiteral("none");
    bool is_spectating = false;
    int spectator_index = 255;
    QJsonArray marshal_zones;
    QJsonArray forecast;

    QJsonObject toJson() const;
};

struct RaceControlMsg {
    int id;
    QString type, text;
    int lap;
    double t;
};

struct StartLights {
    int num = 0;
    bool out = false;
    double t = 0.0;
};

/// Platzhalter-Namen verdeckter Spieler ("Player", "Spieler:in 2" ...).
bool isPlaceholderName(const QString& name);
/// Anzeigename ohne freigegebenen Online-Namen -> z.B. "Ferrari #11".
QString buildFallbackName(const QString& team, int raceNumber, const QString& formulaPrefix);

class RaceState {
public:
    RaceState(SettingsStore& settings, TrackStore& tracks);

    /// Wanduhr in Sekunden (time.time()). Fuer Replays/Tests austauschbar.
    void setClock(std::function<double()> clock) { m_clock = std::move(clock); }
    double now() const { return m_clock(); }

    /// Ein UDP-Datagramm einarbeiten (Header, Session-Wechsel, Dispatch).
    /// Rueckgabe: die Paket-ID, -1 wenn kein gueltiger Header.
    int feed(const QByteArray& d);

    // ── Lesen ────────────────────────────────────────────────────────────────
    const QHash<int, Driver>& drivers() const { return m_drivers; }
    const SessionInfo& session() const { return m_session; }
    const QList<RaceControlMsg>& raceControl() const { return m_raceControl; }
    const QHash<int, QPointF>& carPos() const { return m_carPos; }
    const QList<QJsonObject>& finalClassification() const { return m_final; }
    const QMap<int, QMap<int, int>>& lapPositions() const { return m_lapPositions; }
    const StartLights& startLights() const { return m_lights; }
    const QHash<QString, double>& lastQuali() const { return m_lastQuali; }
    const QSet<int>& validParticipants() const { return m_valid; }
    int playerIndex() const { return m_playerIdx; }
    int rxFormat() const { return m_rxFormat; }
    double lastPacketTime() const { return m_lastPacketTime; }
    bool connected() const;
    const TrackOutline& outline() const { return m_outline; }
    TrackOutline& outline() { return m_outline; }
    TrackLearner& learner() { return m_learner; }
    TrackStore& tracks() { return m_tracks; }
    const TrackStore& tracks() const { return m_tracks; }

    /// Alle Fahrer als Lerner-Sicht (fuer Tests und den Lerner selbst).
    QList<LearnerCar> learnerCars() const;

private:
    Driver& driver(int idx);
    void handleSession(const QByteArray& d);
    void handleParticipants(const QByteArray& d);
    void handleLapData(const QByteArray& d);
    void handleCarStatus(const QByteArray& d);
    void handleCarTelemetry(const QByteArray& d);
    void handleCarTelemetry2(const QByteArray& d);
    void handleMotion(const QByteArray& d);
    void handleFinalClassification(const QByteArray& d);
    void handleCarDamage(const QByteArray& d);
    void handleLapPositions(const QByteArray& d);
    void handleSessionHistory(const QByteArray& d);
    void handleEvent(const QByteArray& d);
    void pushRc(const QString& type, const QString& text);
    QString carName(int idx) const;
    void sessionWechsel();

    SettingsStore& m_settings;
    TrackStore& m_tracks;
    std::function<double()> m_clock;

    QHash<int, Driver> m_drivers;
    struct LapTrack { bool dirty = false; bool valid = true; };
    QHash<int, LapTrack> m_lapTrack;
    SessionInfo m_session;
    QSet<int> m_valid;
    bool m_haveUid = false;
    uint64_t m_lastUid = 0;
    int m_playerIdx = 0;
    QList<RaceControlMsg> m_raceControl;
    int m_rcId = 0;
    QHash<QString, double> m_lastQuali;
    QHash<int, QPointF> m_carPos;
    QList<QJsonObject> m_final;
    QMap<int, QMap<int, int>> m_lapPositions;
    StartLights m_lights;
    double m_lastPacketTime = 0.0;
    int m_rxFormat = 0;
    int m_warnedFormat = -1;
    TrackOutline m_outline;
    TrackLearner m_learner;
};

}  // namespace kers::server
