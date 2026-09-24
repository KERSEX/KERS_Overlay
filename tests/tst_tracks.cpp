// Streckenkonturen: Pruefung, Ablage (eigen schlaegt mitgeliefert), Lernen.
#include "tracks.h"

#include "jsonfile.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace kers::server;

namespace {

constexpr double R = 500.0;
const double UMFANG = 2 * std::numbers::pi * R;             // ~3141,6 m

/// n Punkte auf einem Kreis, `runden` Umlaeufe.
QList<TrackPt> kreis(int nProRunde, double runden = 1.0)
{
    QList<TrackPt> out;
    const int n = static_cast<int>(std::lround(nProRunde * runden));
    for (int i = 0; i <= n; ++i) {
        const double a = 2 * std::numbers::pi * i / nProRunde;
        out.append(TrackPt{R * std::cos(a), R * std::sin(a), std::fmod(double(i) / nProRunde, 1.0), 0});
    }
    return out;
}

QJsonArray alsJson(const QList<TrackPt>& pts)
{
    QJsonArray a;
    for (const TrackPt& p : pts)
        a.append(QJsonArray{p.x, p.z, p.frac, p.sector});
    return a;
}

}  // namespace

class TstTracks : public QObject {
    Q_OBJECT
private slots:
    void rundeTaugtPrueft()
    {
        const int L = static_cast<int>(UMFANG);
        QVERIFY(!rundeTaugt(kreis(50), L, false));                // < 60 Punkte
        QVERIFY(rundeTaugt(kreis(200), L, false));
        QVERIFY(!rundeTaugt(kreis(200), L, true));                // Track Limits
        QVERIFY(rundeTaugt(kreis(200), 0, false));                // ohne Laenge nicht pruefbar
        QVERIFY(!rundeTaugt(kreis(200), L * 2, false));           // halbe Runde
        QVERIFY(!rundeTaugt(kreis(200, 1.5), L, false));          // anderthalb Runden
        QVERIFY(rundeTaugt(kreis(200), static_cast<int>(L / 1.19), false));
    }

    void konturWirdAufEineRundeGekuerzt()
    {
        const int L = static_cast<int>(UMFANG);
        const QList<TrackPt> eine = kreis(200);
        QCOMPARE(konturAufEineRunde(eine, L).size(), eine.size());        // unveraendert
        const QList<TrackPt> zwei = kreis(200, 2.0);
        const QList<TrackPt> gekuerzt = konturAufEineRunde(zwei, L);
        // Bester Punkt ist Index 200 - exakt wieder am Start, kein Spalt
        QCOMPARE(gekuerzt.size(), 201);
        QVERIFY(std::hypot(gekuerzt.last().x - gekuerzt.first().x, gekuerzt.last().z - gekuerzt.first().z) < 0.01);
        QCOMPARE(konturAufEineRunde(zwei, 0).size(), zwei.size());         // ohne Laenge: nichts
    }

    void tracksPruefenIstStreng()
    {
        QJsonObject roh;
        roh["fmt"] = 1;
        roh["11"] = QJsonObject{{"len", 5793}, {"pts", alsJson(kreis(100))}, {"rot", 400}, {"flip", true}};
        roh["12"] = QJsonObject{{"len", 5000}, {"pts", alsJson(kreis(10))}};                 // < 20 Punkte
        QJsonArray kaputt = alsJson(kreis(100));
        kaputt[5] = QJsonArray{1, 2, 3};
        roh["13"] = QJsonObject{{"len", 5000}, {"pts", kaputt}};                               // Punkt mit 3 Werten
        roh["14"] = QJsonObject{{"len", 0}, {"pts", alsJson(kreis(100))}};                   // keine Laenge
        roh["abc"] = QJsonObject{{"len", 5000}, {"pts", alsJson(kreis(100))}};               // keine Zahl
        const auto t = tracksPruefen(roh);
        QCOMPARE(t.size(), 1);
        QVERIFY(t.contains(11));
        QCOMPARE(*t[11].rot, 359);
        QCOMPARE(*t[11].flip, true);
        QCOMPARE(t[11].pts.size(), 101);

        roh["fmt"] = 2;                                                                        // spaeteres Format
        QVERIFY(tracksPruefen(roh).isEmpty());
        roh.remove("fmt");
        QVERIFY(tracksPruefen(roh).isEmpty());
    }

    void eigenSchlaegtMitgeliefert()
    {
        QTemporaryDir dir;
        const QString builtin = dir.filePath("builtin.json");
        const QString user = dir.filePath("tracks.json");
        kers::json::writeObject(builtin, QJsonObject{{"fmt", 1},
            {"11", QJsonObject{{"len", 5793}, {"pts", alsJson(kreis(100))}, {"rot", 45}, {"flip", false}}},
            {"12", QJsonObject{{"len", 4000}, {"pts", alsJson(kreis(100))}}}});
        kers::json::writeObject(user, QJsonObject{{"fmt", 1},
            {"11", QJsonObject{{"len", 5793}, {"pts", alsJson(kreis(150))}}}});

        TrackStore store(builtin, user);
        QVERIFY(store.istEigen(11));
        QCOMPARE(store.eintrag(11)->pts.size(), 151);
        QCOMPARE(store.eintrag(12)->pts.size(), 101);
        QVERIFY(store.eintrag(13) == nullptr);

        int angewandt = 0;
        store.setAusrichtungAnwenden([&](const TrackEntry&) { ++angewandt; });
        TrackOutline o;
        QVERIFY(!store.laden(12, 4031, o));                         // 31 m daneben -> andere Fuehrung
        QVERIFY(o.pts.isEmpty());
        QVERIFY(store.laden(12, 4030, o));
        QVERIFY(o.done && o.fest);
        QCOMPARE(o.ver, 1);
        QCOMPARE(angewandt, 1);
        QVERIFY(store.laden(12, 0, o));                             // ohne Laenge: nicht pruefbar, nehmen

        // Eigene loeschen -> mitgelieferte gilt wieder
        QVERIFY(store.loeschen(11));
        QVERIFY(!store.loeschen(11));
        QCOMPARE(store.eintrag(11)->pts.size(), 101);

        // Ausrichtung bei nur-mitgelieferter Strecke legt eine eigene Kopie an
        store.ausrichtungMerken(12, 90, true);
        QVERIFY(store.istEigen(12));
        store.ausrichtungMerken(99, 10, false);                      // unbekannt -> nichts
        QVERIFY(!store.istEigen(99));
        TrackStore neu(builtin, user);
        QCOMPARE(*neu.eintrag(12)->rot, 90);
        QCOMPARE(*neu.eintrag(12)->flip, true);
        QVERIFY(!neu.istEigen(11));
        const QJsonObject datei = kers::json::readObject(user);
        QCOMPARE(datei.value("12").toObject().value("name").toString(), QStringLiteral("Singapur"));
    }

    void lernenAusEinerRunde()
    {
        QTemporaryDir dir;
        TrackStore store(dir.filePath("keins.json"), dir.filePath("tracks.json"));
        TrackOutline outline;
        TrackLearner learner(store, outline);
        const int tl = static_cast<int>(std::lround(UMFANG));
        learner.streckeGewechselt(7, tl);
        QVERIFY(!outline.fest);

        // Auto 0 faehrt im Kreis, 2 m je Takt, anderthalb Runden
        LearnerCar car{0, 1, false, false, false, false, 1, 0.0, 0};
        double weg = 0.0;
        int abgeschlossen = -1;
        for (int takt = 0; takt < 2400 && abgeschlossen < 0; ++takt) {
            const double a = weg / R;
            QHash<int, QPointF> pos{{0, QPointF(R * std::cos(a), R * std::sin(a))}};
            car.lapNum = 1 + static_cast<int>(weg / tl);
            car.lapDistance = std::fmod(weg, tl);
            learner.learn(pos, {car}, tl, 7);
            if (outline.done)
                abgeschlossen = takt;
            weg += 2.0;
        }
        QVERIFY2(abgeschlossen > 0, "Runde wurde nicht geschlossen");
        QVERIFY(outline.pts.size() > 500);             // alle ~6 m ein Punkt
        QVERIFY(store.istEigen(7));                                  // gespeichert
        QCOMPARE(outline.ver, 2);                                    // Wechsel + fertig
        QVERIFY(learner.refIdx().has_value());

        // Neue Session: gelernt, aber nicht "fest" -> Karte geht (wie 0.2.6) neu in den Lernmodus
        learner.sessionNeu();
        QVERIFY(outline.pts.isEmpty());

        // Bekannte Strecke: bleibt beim Session-Wechsel stehen (bewusste Abweichung von 0.2.6)
        QVERIFY(learner.streckeGewechselt(7, tl));
        QVERIFY(outline.fest);
        const int n = outline.pts.size();
        learner.sessionNeu();
        QCOMPARE(outline.pts.size(), n);
        // und wird nicht mehr angefasst
        learner.learn({{0, QPointF(0, 0)}}, {car}, tl, 7);
        QCOMPARE(outline.pts.size(), n);

        // Neu lernen hebt "fest" auf
        learner.neuLernen();
        QVERIFY(!outline.fest);
        QVERIFY(outline.pts.isEmpty());
    }

    void ungueltigeRundeZaehltNicht()
    {
        QTemporaryDir dir;
        TrackStore store(dir.filePath("keins.json"), dir.filePath("tracks.json"));
        TrackOutline outline;
        TrackLearner learner(store, outline);
        const int tl = static_cast<int>(std::lround(UMFANG));
        learner.streckeGewechselt(7, tl);
        LearnerCar car{0, 1, false, false, false, false, 1, 0.0, 0};
        for (double weg = 0.0; weg < tl * 1.1; weg += 2.0) {
            const double a = weg / R;
            car.lapNum = 1 + static_cast<int>(weg / tl);
            car.lapDistance = std::fmod(weg, tl);
            car.lapInvalid = weg > 500 && weg < 520;
            learner.learn({{0, QPointF(R * std::cos(a), R * std::sin(a))}}, {car}, tl, 7);
        }
        QVERIFY(!outline.done);
        QVERIFY(!store.istEigen(7));
    }

    void boxengasseWechseltReferenz()
    {
        QTemporaryDir dir;
        TrackStore store(dir.filePath("keins.json"), dir.filePath("tracks.json"));
        TrackOutline outline;
        TrackLearner learner(store, outline);
        learner.streckeGewechselt(7, 3000);
        const LearnerCar a{0, 1, true, false, false, false, 1, 10.0, 0};     // P1, aber in der Box
        const LearnerCar b{1, 2, false, false, false, false, 1, 10.0, 0};
        learner.learn({{0, QPointF(1, 1)}, {1, QPointF(2, 2)}}, {a, b}, 3000, 7);
        QCOMPARE(*learner.refIdx(), 1);
    }
};

QTEST_GUILESS_MAIN(TstTracks)
#include "tst_tracks.moc"
