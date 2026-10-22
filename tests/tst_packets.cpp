// Paket-Parser und Rennzustand: jedes gelesene Feld je PID, alle 13 Events,
// Session-Wechsel, Payload - und abgeschnittene Pakete (kein Absturz).
#include "packet_builder.h"

#include "constants.h"
#include "packets.h"
#include "payload.h"
#include "race_state.h"
#include "settings_store.h"
#include "tracks.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace kers::server;
using kers::test::Pkt;
using kers::test::HDR;
namespace T = kers::test;

namespace {

// ── Paketbauer mit festen Testwerten ─────────────────────────────────────────

Pkt participants(uint64_t uid = 42)
{
    Pkt p(4, T::SIZE_PARTICIPANTS, uid);
    const int base = HDR + 1;
    p.u8(HDR, 2);
    for (int i = 0; i < 24; ++i)
        p.u16(base + i * 60 + 5, 65535);           // unbelegt
    // Auto 0: Ferrari #16 "Leclerc"
    p.u16(base + 5, 1).u8(base + 8, 16).str(base + 10, "Leclerc");
    // Auto 1: McLaren ('26-Id 484) #4, verdeckt ("Player")
    p.u16(base + 60 + 5, 484).u8(base + 60 + 8, 4).str(base + 60 + 10, "Player");
    return p;
}

Pkt session(int type = 15, int trackId = 11, int formula = 13, uint64_t uid = 42)
{
    Pkt p(1, T::SIZE_SESSION_MIN + 3 * 8, uid);
    const int b = HDR;
    p.u8(b + 0, 3).u8(b + 1, 30).u8(b + 2, 22).u8(b + 3, 50).u16(b + 4, 5793);
    p.u8(b + 6, type).i8(b + 7, trackId).u8(b + 8, formula).u16(b + 9, 1800);
    p.u8(b + 15, 1).u8(b + 16, 1);                  // Zuschauer, Kamera auf Auto 1
    p.u8(b + 18, 2);                                 // zwei Marshal-Zonen
    p.f32(b + 19, 0.1f).i8(b + 23, 3);
    p.f32(b + 24, 1.5f).i8(b + 28, 0);               // ausserhalb 0..1 -> verworfen
    p.u8(b + 124, 2);                                // VSC
    p.u8(b + 126, 3);                                // drei Vorhersage-Samples
    const int fc = b + 127;
    p.u8(fc + 0, type).u8(fc + 1, 0).u8(fc + 2, 3).u8(fc + 7, 40);        // jetzt
    p.u8(fc + 8, type).u8(fc + 9, 5).u8(fc + 10, 4).u8(fc + 15, 70);      // +5 min
    p.u8(fc + 16, 10).u8(fc + 17, 10).u8(fc + 18, 1).u8(fc + 23, 0);      // andere Session
    return p;
}

struct LapRow {
    uint32_t last = 0, cur = 0;
    int s1ms = 0, s1min = 0, s2ms = 0, s2min = 0;
    int gf = 0, gfm = 0, gl = 0, glm = 0;
    float dist = 0;
    int pos = 0, lap = 0, pit = 0, stops = 0, sector = 0, invalid = 0, pen = 0;
    int corner = 0, dt = 0, sg = 0, grid = 0, status = 0, result = 0, pitMs = 0;
};

Pkt lapData(const LapRow& a, const LapRow& b, uint64_t uid = 42)
{
    Pkt p(2, T::SIZE_LAPDATA, uid);
    auto put = [&](int i, const LapRow& r) {
        const int s = HDR + i * 57;
        p.u32(s + 0, r.last).u32(s + 4, r.cur);
        p.u16(s + 8, r.s1ms).u8(s + 10, r.s1min).u16(s + 11, r.s2ms).u8(s + 13, r.s2min);
        p.u16(s + 14, r.gf).u8(s + 16, r.gfm).u16(s + 17, r.gl).u8(s + 19, r.glm);
        p.f32(s + 20, r.dist);
        p.u8(s + 32, r.pos).u8(s + 33, r.lap).u8(s + 34, r.pit).u8(s + 35, r.stops);
        p.u8(s + 36, r.sector).u8(s + 37, r.invalid).u8(s + 38, r.pen);
        p.u8(s + 40, r.corner).u8(s + 41, r.dt).u8(s + 42, r.sg).u8(s + 43, r.grid);
        p.u8(s + 44, r.status).u8(s + 45, r.result).u16(s + 49, r.pitMs);
    };
    put(0, a);
    put(1, b);
    return p;
}

LapRow car0()
{
    LapRow r;
    r.last = 83456; r.cur = 12000;
    r.s1ms = 25000; r.s2ms = 5000; r.s2min = 1;
    r.gf = 1500; r.gl = 500; r.glm = 1;
    r.dist = 1234.5f;
    r.pos = 2; r.lap = 5; r.stops = 1; r.sector = 1; r.pen = 3;
    r.corner = 2; r.dt = 1; r.sg = 1; r.grid = 4; r.status = 4; r.result = 2; r.pitMs = 2345;
    return r;
}

LapRow car1()
{
    LapRow r;
    r.last = 82000; r.pos = 1; r.lap = 6; r.dist = 2000.0f; r.result = 2; r.invalid = 1;
    return r;
}

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<TrackStore> tracks;
    std::unique_ptr<RaceState> st;
    double clock = 1000.0;

    Fixture()
    {
        settings = std::make_unique<SettingsStore>(dir.path());
        tracks = std::make_unique<TrackStore>(dir.filePath("gibtsnicht.json"), dir.filePath("tracks.json"));
        st = std::make_unique<RaceState>(*settings, *tracks);
        st->setClock([this] { return clock; });
    }
    void base()
    {
        st->feed(session());
        st->feed(participants());
        st->feed(lapData(car0(), car1()));
    }
};

}  // namespace

class TstPackets : public QObject {
    Q_OBJECT
private slots:
    void header()
    {
        const Pkt p(6, 40, 0x1122334455667788ull, 7);
        const PacketHeader h = parseHeader(p);
        QVERIFY(h.ok);
        QCOMPARE(int(h.format), 2026);
        QCOMPARE(int(h.packetId), 6);
        QCOMPARE(h.sessionUid, 0x1122334455667788ull);
        QCOMPARE(int(h.playerIndex), 7);
        QVERIFY(!parseHeader(QByteArray(28, '\0')).ok);

        Fixture f;
        QCOMPARE(f.st->feed(QByteArray(10, '\0')), -1);
        QCOMPARE(f.st->feed(p), 6);
        QCOMPARE(f.st->rxFormat(), 2026);
        QCOMPARE(f.st->playerIndex(), 7);
        QVERIFY(f.st->connected());
        f.clock += 3.5;
        QVERIFY(!f.st->connected());
    }

    void sessionPacket()
    {
        Fixture f;
        f.st->feed(session());
        const SessionInfo& s = f.st->session();
        QCOMPARE(s.weather, 3);
        QCOMPARE(s.weather_name, QString::fromUtf8("Leichter Regen 🌦️"));
        QCOMPARE(s.track_temp, 30);
        QCOMPARE(s.air_temp, 22);
        QCOMPARE(s.total_laps, 50);
        QCOMPARE(s.track_length, 5793);
        QCOMPARE(s.type, 15);
        QCOMPARE(s.type_name, QStringLiteral("Rennen"));
        QVERIFY(!s.is_quali);
        QVERIFY(s.has_track_id);
        QCOMPARE(s.track_id, 11);
        QCOMPARE(s.track_name, QStringLiteral("Monza"));
        QCOMPARE(s.track_colors.size(), 3);
        QVERIFY(!s.track_known);
        QCOMPARE(s.formula_name, QStringLiteral("F1 26"));
        QCOMPARE(s.time_left, 1800);
        QCOMPARE(s.safety_car_status, QStringLiteral("vsc"));
        QVERIFY(s.is_spectating);
        QCOMPARE(s.spectator_index, 1);
        QCOMPARE(s.marshal_zones.size(), 1);
        QCOMPARE(s.marshal_zones.at(0).toObject().value("f").toInt(), 3);
        QCOMPARE(s.marshal_zones.at(0).toObject().value("s").toDouble(), 0.1);
        QCOMPARE(s.weather_rain, 40);
        QCOMPARE(s.forecast.size(), 1);
        QCOMPARE(s.forecast.at(0).toObject().value("t").toInt(), 5);
        QCOMPARE(s.forecast.at(0).toObject().value("rain").toInt(), 70);

        // trackId ist int8: -1 = unbekannt
        Fixture g;
        g.st->feed(session(15, -1));
        QCOMPARE(g.st->session().track_id, -1);
        QCOMPARE(g.st->session().track_name, QStringLiteral("Unbekannte Strecke"));
        Fixture h;
        h.st->feed(session(15, 77));
        QCOMPARE(h.st->session().track_name, QStringLiteral("Strecke #77"));
    }

    void participantsPacket()
    {
        Fixture f;
        f.st->feed(session());
        f.st->feed(participants());
        QCOMPARE(f.st->validParticipants().size(), 2);
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.name, QStringLiteral("Leclerc"));
        QCOMPARE(a.team, QStringLiteral("Ferrari"));
        QCOMPARE(a.race_number, 16);
        QVERIFY(!a.name_hidden);
        const Driver& b = f.st->drivers()[1];
        QCOMPARE(b.name, QStringLiteral("McLaren #4"));
        QCOMPARE(b.team_id, 484);
        QVERIFY(b.name_hidden);
        QVERIFY(!f.st->drivers().contains(2));
    }

    void placeholderNames()
    {
        QVERIFY(isPlaceholderName(""));
        QVERIFY(isPlaceholderName("Player"));
        QVERIFY(isPlaceholderName("Player 1"));
        QVERIFY(isPlaceholderName("SPIELER:IN"));
        QVERIFY(isPlaceholderName("Spieler:in 2"));
        QVERIFY(!isPlaceholderName("Playerone"));
        QCOMPARE(buildFallbackName("Ferrari", 11, "F1 26"), QStringLiteral("Ferrari #11"));
        QCOMPARE(buildFallbackName("Ferrari", 0, "F1 26"), QStringLiteral("Ferrari"));
        QCOMPARE(buildFallbackName("", 7, "F1 26"), QStringLiteral("F1 26 #7"));
        QCOMPARE(buildFallbackName("", 0, "F1"), QStringLiteral("F1 #?"));
    }

    void lapDataPacket()
    {
        Fixture f;
        f.base();
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.last_lap, 83.456);
        QCOMPARE(a.best_lap, 83.456);
        QCOMPARE(a.current_lap_time, 12.0);
        QCOMPARE(a.sector1, 25.0);
        QCOMPARE(a.sector2, 65.0);
        QCOMPARE(a.gap_to_ahead, 1.5);
        QCOMPARE(a.gap_to_leader, 60.5);
        QCOMPARE(a.lap_distance, 1234.5);
        QCOMPARE(a.position, 2);
        QCOMPARE(a.lap_num, 5);
        QCOMPARE(a.pit_stops, 1);
        QCOMPARE(a.sector, 1);
        QCOMPARE(a.penalties, 3 + STOPGO_SEK);          // m_penalties + 10 x Stop-&-Go
        QCOMPARE(a.pen_dt, 1);
        QCOMPARE(a.corner_warnings, 2);
        QCOMPARE(a.grid_position, 4);
        QCOMPARE(a.driver_status, 4);
        QCOMPARE(a.pit_time, 2.345);
        QVERIFY(!a.in_pit);
        QVERIFY(!a.finished && !a.dnf && !a.dsq);
        QCOMPARE(f.st->session().current_lap, 6);

        // schnellste Runde: Auto 1 mit 82,0
        QCOMPARE(f.st->session().fastest_lap_time, 82.0);
        QCOMPARE(f.st->session().fastest_lap_driver, QStringLiteral("McLaren #4"));

        // Auto 1 war in Runde 6 ungueltig -> die 81,0 aus Runde 6 zaehlt nicht
        LapRow b = car1();
        b.lap = 7; b.last = 81000; b.invalid = 0;
        LapRow a2 = car0();
        a2.pitMs = 0;                                    // Standzeit bleibt stehen (sticky)
        a2.result = 4;
        f.st->feed(lapData(a2, b));
        QCOMPARE(f.st->drivers()[1].last_lap, 81.0);
        QCOMPARE(f.st->drivers()[1].best_lap, 82.0);
        QCOMPARE(f.st->session().fastest_lap_time, 82.0);
        QCOMPARE(f.st->drivers()[0].pit_time, 2.345);
        QVERIFY(f.st->drivers()[0].dnf);

        // Runde 8 gueltig -> zaehlt
        b.lap = 8; b.last = 80000;
        f.st->feed(lapData(a2, b));
        QCOMPARE(f.st->drivers()[1].best_lap, 80.0);
        QCOMPARE(f.st->session().fastest_lap_time, 80.0);
    }

    void lapDataSkipsEmptyAndPhantomSlots()
    {
        Fixture f;
        f.st->feed(participants());
        LapRow ghost;
        ghost.pos = 3; ghost.result = 2;                 // Slot 2 ist laut Participants leer
        Pkt p = lapData(car0(), car1());
        const int s = HDR + 2 * 57;
        p.u8(s + 32, 3).u8(s + 45, 2);
        f.st->feed(p);
        QVERIFY(!f.st->drivers().contains(2));
        LapRow noResult = car0();
        noResult.result = 0;
        Fixture g;
        g.st->feed(lapData(noResult, LapRow{}));
        QVERIFY(!g.st->drivers().contains(0));
    }

    void lapDataFallbackName()
    {
        // Lap-Daten VOR dem Participants-Paket -> trotzdem ein Name
        Fixture f;
        f.st->feed(lapData(car0(), car1()));
        QCOMPARE(f.st->drivers()[0].name, QStringLiteral("F1 #?"));
    }

    void carStatus()
    {
        Fixture f;
        Pkt p(7, T::SIZE_STATUS);
        p.u8(HDR + 26, 16).u8(HDR + 27, 7).f32(HDR + 37, 2000000.0f).u8(HDR + 41, 2);
        f.st->feed(p);
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.compound, QStringLiteral("S"));
        QCOMPARE(a.tyre_age, 7);
        QCOMPARE(a.ers_pct, 50.0);
        QCOMPARE(a.ers_mode, 2);
        QCOMPARE(a.stints, QStringList{"S"});
        p.u8(HDR + 26, 17).f32(HDR + 37, 9e6f);
        f.st->feed(p);
        QCOMPARE(f.st->drivers()[0].stints, (QStringList{"S", "M"}));
        QCOMPARE(f.st->drivers()[0].ers_pct, 100.0);
        p.u8(HDR + 26, 99);                              // unbekannt -> "?" und kein Stint
        f.st->feed(p);
        QCOMPARE(f.st->drivers()[0].compound, QStringLiteral("?"));
        QCOMPARE(f.st->drivers()[0].stints.size(), 2);
        QCOMPARE(compoundLetter(8), QStringLiteral("W"));
        QCOMPARE(compoundLetter(7), QStringLiteral("I"));
    }

    void telemetry()
    {
        Fixture f;
        Pkt p(6, T::SIZE_TELEMETRY);
        p.u16(HDR + 0, 312).f32(HDR + 2, 0.876f).f32(HDR + 10, 1.5f).i8(HDR + 15, -1).u8(HDR + 18, 1);
        f.st->feed(p);
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.speed, 312);
        QCOMPARE(a.throttle, 0.88);
        QCOMPARE(a.brake, 1.0);
        QCOMPARE(a.gear, -1);
        QVERIFY(a.drs);

        Pkt t2(16, T::SIZE_TELEM2);
        t2.u8(HDR + 4, 1).u8(HDR + 10 + 5, 1);
        f.st->feed(t2);
        QVERIFY(f.st->drivers()[0].overtake_available);
        QVERIFY(!f.st->drivers()[0].overtake_active);
        QVERIFY(f.st->drivers()[1].overtake_active);
    }

    void motion()
    {
        Fixture f;
        Pkt p(0, T::SIZE_MOTION);
        p.f32(HDR + 0, 100.26f).f32(HDR + 8, -50.04f);
        p.f32(HDR + 54, 3.0e38f).f32(HDR + 54 + 8, -3.0e38f);
        f.st->feed(p);
        QCOMPARE(f.st->carPos().size(), 23);             // alle Nullen sind "gueltig", nur Auto 1 nicht
        QVERIFY(!f.st->carPos().contains(1));
        QCOMPARE(f.st->carPos()[0].x(), 100.3);
        QCOMPARE(f.st->carPos()[0].y(), -50.0);
    }

    void damage()
    {
        Fixture f;
        Pkt p(10, T::SIZE_DAMAGE);
        p.f32(HDR + 0, 10).f32(HDR + 4, 20).f32(HDR + 8, 30).f32(HDR + 12, 41);
        p.u8(HDR + 28, 5).u8(HDR + 29, 6).u8(HDR + 30, 7);
        f.st->feed(p);
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.tyre_wear, 25);
        QCOMPARE(a.dmg_fl, 5);
        QCOMPARE(a.dmg_fr, 6);
        QCOMPARE(a.dmg_rw, 7);
    }

    void finalClassification()
    {
        Fixture f;
        f.base();
        Pkt p(8, T::SIZE_FINAL);
        const int b = HDR + 1;
        p.u8(HDR, 3);
        p.u8(b + 0, 2).u8(b + 1, 50).u8(b + 2, 4).u8(b + 3, 18).u8(b + 4, 1).u8(b + 5, 3);
        p.u32(b + 7, 81500).f64(b + 11, 5000.5).u8(b + 19, 5).u8(b + 21, 2).u8(b + 30, 16).u8(b + 31, 17);
        p.u8(b + 46 + 0, 1).u8(b + 46 + 5, 3);
        p.u8(b + 92 + 0, 3).u8(b + 92 + 5, 1);          // inaktiv -> kein Ergebnis
        f.st->feed(p);
        const auto& fc = f.st->finalClassification();
        QCOMPARE(fc.size(), 2);
        QCOMPARE(fc.at(0).value("index").toInt(), 1);
        QCOMPARE(fc.at(1).value("name").toString(), QStringLiteral("Leclerc"));
        QCOMPARE(fc.at(1).value("team").toString(), QStringLiteral("Ferrari"));
        QCOMPARE(fc.at(1).value("best_lap").toDouble(), 81.5);
        QCOMPARE(fc.at(1).value("total_time").toDouble(), 5005.5);
        QCOMPARE(fc.at(1).value("penalties").toInt(), 5);
        QCOMPARE(fc.at(1).value("stints").toArray(), (QJsonArray{"S", "M"}));
        QCOMPARE(fc.at(1).value("points").toInt(), 18);
    }

    void lapPositions()
    {
        Fixture f;
        f.base();
        Pkt p(15, T::SIZE_LAPPOS);
        p.u8(HDR, 2).u8(HDR + 1, 0);
        p.u8(HDR + 2 + 0, 2).u8(HDR + 2 + 1, 1);
        p.u8(HDR + 2 + 24 + 0, 1).u8(HDR + 2 + 24 + 1, 2);
        f.st->feed(p);
        QCOMPARE(f.st->lapPositions().size(), 2);
        QCOMPARE(f.st->lapPositions()[1][0], 2);
        QCOMPARE(f.st->lapPositions()[2][0], 1);
        const QJsonObject j = lapPositionsJson(*f.st);
        QCOMPARE(j.value("laps").toObject().value("2").toObject().value("1").toInt(), 2);
        QCOMPARE(j.value("drivers").toObject().value("0").toObject().value("name").toString(), QStringLiteral("Leclerc"));
    }

    void sessionHistory()
    {
        Fixture f;
        f.base();
        Pkt p(11, T::SIZE_HISTORY);
        const int b = HDR;
        p.u8(b, 0).u8(b + 3, 2).u8(b + 4, 1).u8(b + 5, 2).u8(b + 6, 2);
        const int l1 = b + 7, l2 = b + 7 + 14;
        p.u32(l1, 90000).u16(l1 + 4, 28500);
        p.u32(l2, 79500).u16(l2 + 7, 26000).u16(l2 + 10, 5000).u8(l2 + 12, 1);
        f.st->feed(p);
        const Driver& a = f.st->drivers()[0];
        QCOMPARE(a.best_lap, 79.5);
        QCOMPARE(a.best_sectors[0], 28.5);
        QCOMPARE(a.best_sectors[1], 26.0);
        QCOMPARE(a.best_sectors[2], 65.0);
        // Sektoren genau der Bestrunde (Runde 2) - S1 ist dort leer geblieben
        QCOMPARE(a.best_lap_sectors[0], 0.0);
        QCOMPARE(a.best_lap_sectors[1], 26.0);
        QCOMPARE(a.best_lap_sectors[2], 65.0);
        QCOMPARE(f.st->session().fastest_lap_time, 79.5);
        QCOMPARE(f.st->session().fastest_lap_driver, QStringLiteral("Leclerc"));
        Pkt bad(11, T::SIZE_HISTORY);
        bad.u8(HDR, 30);                                  // Index ausserhalb -> nichts
        f.st->feed(bad);
        QVERIFY(!f.st->drivers().contains(30));
    }

    void events()
    {
        Fixture f;
        f.base();
        auto last = [&] { return f.st->raceControl().last(); };

        f.st->feed(T::event("PENA", QByteArray("\x04\x00\x00\xff\x05\x00\x00", 7)));
        QCOMPARE(last().type, QStringLiteral("penalty"));
        QCOMPARE(last().text, QStringLiteral("Leclerc: Zeitstrafe +5s"));
        QCOMPARE(last().lap, 6);
        f.st->feed(T::event("PENA", QByteArray("\x0b\x00\x01\xff\x00\x00\x00", 7)));
        QCOMPARE(last().type, QStringLiteral("tracklimit"));
        QCOMPARE(last().text, QStringLiteral("McLaren #4: Track Limits"));
        f.st->feed(T::event("PENA", QByteArray("\x01\x00\x00\xff\x00\x00\x00", 7)));
        QCOMPARE(last().text, QStringLiteral("Leclerc: 10 Sek Strafe"));
        f.st->feed(T::event("PENA", QByteArray("\x63\x00\x09\xff\x00\x00\x00", 7)));
        QCOMPARE(last().text, QStringLiteral("Auto #9: Strafe"));

        f.st->feed(T::event("DRSE"));
        QCOMPARE(last().text, QStringLiteral("MOM / OVERRIDE AKTIV"));
        f.st->feed(T::event("DRSD"));
        QCOMPARE(last().text, QStringLiteral("MOM / OVERRIDE AUS"));

        QByteArray ftlp(5, '\0');
        ftlp[0] = 1;
        const float t = 81.234f;
        std::memcpy(ftlp.data() + 1, &t, 4);
        f.st->feed(T::event("FTLP", ftlp));
        QCOMPARE(last().type, QStringLiteral("fl"));
        QCOMPARE(last().text, QStringLiteral("McLaren #4: Schnellste Runde 1:21.234"));
        QCOMPARE(f.st->session().fastest_lap_time, 81.234);

        f.st->feed(T::event("RTMT", QByteArray(1, '\0')));
        QCOMPARE(last().text, QStringLiteral("Leclerc: Aufgabe (DNF)"));
        f.st->feed(T::event("RDFL"));
        QCOMPARE(last().type, QStringLiteral("redflag"));
        f.st->feed(T::event("CHQF"));
        QCOMPARE(last().text, QStringLiteral("Zielflagge"));
        f.st->feed(T::event("RCWN", QByteArray(1, '\1')));
        QCOMPARE(last().text, QStringLiteral("McLaren #4 gewinnt das Rennen!"));
        f.st->feed(T::event("DTSV", QByteArray(1, '\0')));
        QCOMPARE(last().text, QStringLiteral("Leclerc: Drive Through Penalty abgeleistet"));
        f.st->feed(T::event("SGSV", QByteArray(1, '\0')));
        QCOMPARE(last().text, QStringLiteral("Leclerc: 10 Sek Strafe abgeleistet"));
        QCOMPARE(last().type, QStringLiteral("penserved"));

        f.clock = 2000.0;
        f.st->feed(T::event("STLG", QByteArray(1, '\3')));
        QCOMPARE(f.st->startLights().num, 3);
        QVERIFY(!f.st->startLights().out);
        QCOMPARE(f.st->startLights().t, 2000.0);
        f.clock = 2001.25;
        f.st->feed(T::event("LGOT"));
        QVERIFY(f.st->startLights().out);
        f.clock = 2002.0;
        const QJsonObject pl = buildLivePayload(*f.st, {}, {});
        QCOMPARE(pl.value("start_lights").toObject().value("age").toDouble(), 0.75);

        f.st->feed(T::event("SSTA"));
        QCOMPARE(f.st->startLights().num, 0);
        QCOMPARE(f.st->startLights().t, 0.0);
        QCOMPARE(buildLivePayload(*f.st, {}, {}).value("start_lights").toObject().value("age").toDouble(), 9999.0);

        // Ohne F1-26-Formel heisst es DRS
        Fixture g;
        g.st->feed(session(15, 11, 0));
        g.st->feed(T::event("DRSE"));
        QCOMPARE(g.st->raceControl().last().text, QStringLiteral("DRS AKTIV"));

        // Nur RC_KEEP Meldungen, die ids laufen weiter
        for (int i = 0; i < 40; ++i)
            g.st->feed(T::event("CHQF"));
        QCOMPARE(g.st->raceControl().size(), RC_KEEP);
        QCOMPARE(g.st->raceControl().last().id, 41);
    }

    void sessionChange()
    {
        Fixture f;
        f.st->feed(session(5));                           // Quali
        f.st->feed(participants());
        f.st->feed(lapData(car0(), car1()));
        f.st->feed(T::event("CHQF"));
        QVERIFY(f.st->session().is_quali);
        QCOMPARE(f.st->drivers().size(), 2);

        f.st->feed(session(15, 11, 13, 99));              // neue Session-UID
        QCOMPARE(f.st->lastQuali().size(), 2);
        QCOMPARE(f.st->lastQuali().value("Leclerc"), 83.456);
        QCOMPARE(f.st->lastQuali().value("McLaren #4"), 82.0);
        QVERIFY(f.st->drivers().isEmpty());
        QVERIFY(f.st->raceControl().isEmpty());
        QCOMPARE(f.st->session().current_lap, 0);
        QCOMPARE(f.st->session().fastest_lap_time, 0.0);
        QVERIFY(!f.st->session().is_quali);
    }

    void payload()
    {
        Fixture f;
        f.base();
        const QJsonObject settings{{"x", 1}};
        const QJsonObject regie{{"chart", "none"}};
        const QJsonObject pl = buildLivePayload(*f.st, settings, regie);
        const QJsonArray drv = pl.value("drivers").toArray();
        QCOMPARE(drv.size(), 2);
        QCOMPARE(drv.at(0).toObject().value("index").toInt(), 1);          // nach Position
        QCOMPARE(drv.at(1).toObject().value("laps_down").toInt(), 1);      // 5,21 vs 6,35 Runden
        QCOMPARE(drv.at(0).toObject().value("laps_down").toInt(), 0);
        QCOMPARE(pl.value("focus_index").toInt(), 1);                      // Zuschauer auf Auto 1
        QCOMPARE(pl.value("settings").toObject(), settings);
        QCOMPARE(pl.value("regie").toObject(), regie);
        QCOMPARE(pl.value("udp").toObject().value("want").toInt(), 2026);
        QVERIFY(pl.value("connected").toBool());
        QVERIFY(pl.contains("race_control"));
        QVERIFY(pl.contains("final_classification"));
        QVERIFY(pl.contains("quali_results"));
        QCOMPARE(pl.value("session").toObject().value("track_name").toString(), QStringLiteral("Monza"));
        const QJsonObject d0 = drv.at(1).toObject();
        for (const char* k : {"name", "team", "team_id", "position", "gap_to_leader", "gap_to_ahead", "compound",
                              "tyre_age", "last_lap", "current_lap_time", "sector1", "sector2", "drs", "ers_pct",
                              "ers_mode", "in_pit", "dnf", "dsq", "finished", "race_number", "best_lap",
                              "driver_status", "pit_time", "overtake_active", "overtake_available", "lap_invalid",
                              "penalties", "pen_dt", "corner_warnings", "pit_stops", "stints", "lap_num",
                              "lap_distance", "laps_down", "sector", "grid_position", "speed", "throttle", "brake",
                              "gear", "dmg_fl", "dmg_fr", "dmg_rw", "tyre_wear", "best_sectors", "best_lap_sectors", "name_hidden", "index"})
            QVERIFY2(d0.contains(QLatin1String(k)), k);
        QVERIFY(!d0.contains("pos_xz"));

        // Quali: keine Ueberrundungen
        Fixture q;
        q.st->feed(session(5));
        q.st->feed(participants());
        q.st->feed(lapData(car0(), car1()));
        const QJsonArray qd = buildLivePayload(*q.st, {}, {}).value("drivers").toArray();
        QCOMPARE(qd.at(1).toObject().value("laps_down").toInt(), 0);
    }

    void truncatedPacketsDoNotCrash()
    {
        const QList<QByteArray> full = {
            Pkt(0, T::SIZE_MOTION), session(), lapData(car0(), car1()), T::event("PENA", "\x04\x00\x00\xff\x05\x00\x00"),
            T::event("FTLP", QByteArray(5, '\1')), T::event("STLG", QByteArray(1, '\5')), participants(),
            Pkt(6, T::SIZE_TELEMETRY), Pkt(7, T::SIZE_STATUS), Pkt(8, T::SIZE_FINAL).u8(HDR, 24).fill(HDR + 1, 24 * 46, 3),
            Pkt(10, T::SIZE_DAMAGE), Pkt(11, T::SIZE_HISTORY).fill(HDR + 3, 4, 100).fill(HDR + 7, 1400, 0xff),
            Pkt(15, T::SIZE_LAPPOS).u8(HDR, 255).fill(HDR + 2, 1200, 1), Pkt(16, T::SIZE_TELEM2)};
        Fixture f;
        f.base();
        for (const QByteArray& p : full) {
            for (int n = 0; n <= p.size(); ++n)
                f.st->feed(p.left(n));
            // Muell hinter einem gueltigen Header
            QByteArray junk = p;
            for (int i = HDR; i < junk.size(); ++i)
                junk[i] = static_cast<char>((i * 131) & 0xff);
            f.st->feed(junk);
        }
        // Und dazu ein paar Pakete mit unbekannter ID / fremdem Format
        f.st->feed(Pkt(99, 200));
        Pkt old(2, T::SIZE_LAPDATA);
        old.u16(0, 2025);
        f.st->feed(old);
        QCOMPARE(f.st->rxFormat(), 2025);
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(TstPackets)
#include "tst_packets.moc"
