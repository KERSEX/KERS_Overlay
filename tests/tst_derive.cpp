// derive: Sektorfarben und das Stehenlassen des Quali-Ergebnisses.
#include "derive.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace kers::hud;

namespace {

QJsonObject driver(int idx, int pos, double s1, double s2, double last, double best = 0.0)
{
    return QJsonObject{
        {"index", idx}, {"position", pos}, {"driver_status", 4}, {"in_pit", false},
        {"sector1", s1}, {"sector2", s2}, {"last_lap", last}, {"best_lap", best},
        {"current_lap_time", 0.0}, {"sector", 2}, {"ers_mode", 2}, {"ers_pct", 80.0},
    };
}

QJsonObject payload(bool quali, const QJsonArray& drivers, bool connected = true, int lap = 1)
{
    QJsonObject session{{"type", quali ? 5 : 10}, {"is_quali", quali}, {"current_lap", lap}};
    return QJsonObject{{"connected", connected}, {"drivers", drivers}, {"session", session},
                       {"start_lights", QJsonObject{{"num", 0}, {"out", false}, {"age", 9999}}}};
}

}  // namespace

class TstDerive : public QObject {
    Q_OBJECT
private slots:
    void sektorfarben_sessionBestLila_persoenlichGruen_langsamerGelb()
    {
        Config cfg;
        SharedState sh(cfg);
        // Zwei Fahrer in der Quali: Fahrer 1 setzt S1 30.0, Fahrer 2 S1 30.5.
        QJsonArray drivers{driver(1, 1, 30.0, 40.0, 0.0), driver(2, 2, 30.5, 40.5, 0.0)};
        const DeriveResult r = sh.derive(payload(true, drivers));
        QCOMPARE(r.drivers.size(), 2);
        // Fahrer 1: beide Sektoren Session-Best -> "sp"
        SectorView v1 = sh.sectorView(r.drivers.at(0), true, QStringLiteral("track"));
        QCOMPARE(v1.classes[0], QStringLiteral("sp"));
        QCOMPARE(v1.classes[1], QStringLiteral("sp"));
        QCOMPARE(v1.classes[2], QString());          // S3 noch nicht gefahren
        // Fahrer 2: persoenliche Bestzeit, aber nicht Session-Best -> "sg"
        SectorView v2 = sh.sectorView(r.drivers.at(1), true, QStringLiteral("track"));
        QCOMPARE(v2.classes[0], QStringLiteral("sg"));
        // Naechste Runde: Fahrer 2 faehrt S1 langsamer als seine Bestzeit -> "sy"
        QJsonArray drivers2{driver(1, 1, 30.0, 40.0, 0.0), driver(2, 2, 31.0, 40.5, 0.0)};
        const DeriveResult r2 = sh.derive(payload(true, drivers2));
        SectorView v3 = sh.sectorView(r2.drivers.at(1), true, QStringLiteral("track"));
        QCOMPARE(v3.classes[0], QStringLiteral("sy"));
    }

    void imRennenKeineEinfaerbung()
    {
        Config cfg;
        SharedState sh(cfg);
        QJsonArray drivers{driver(1, 1, 30.0, 40.0, 0.0)};
        const DeriveResult r = sh.derive(payload(false, drivers));
        SectorView v = sh.sectorView(r.drivers.at(0), false, QString());
        QCOMPARE(v.classes[0], QString());
        QCOMPARE(v.times[0], 30.0);
    }

    void qualiErgebnisBleibtStehen_bisAmpelOderRennen()
    {
        Config cfg;
        SharedState sh(cfg);
        double now = 0.0;
        sh.setClock([&now]() { return now; });
        QJsonArray q{driver(1, 1, 30.0, 40.0, 90.0, 90.0)};
        sh.maybeHoldQuali(payload(true, q));
        // Das Spiel schaltet um: Rennen, aber noch keine Runde und keine Fahrer.
        QJsonObject rennen = payload(false, QJsonArray(), true, 0);
        QJsonObject held = sh.maybeHoldQuali(rennen);
        QVERIFY(held.value("session").toObject().value("_quali_result").toBool());
        QCOMPARE(held.value("drivers").toArray().size(), 1);
        QCOMPARE(held.value("focus_index").toInt(), -1);
        // Startampel -> Ergebnis faellt.
        QJsonObject ampel = rennen;
        ampel["start_lights"] = QJsonObject{{"num", 3}, {"out", false}, {"age", 1.0}};
        QVERIFY(!sh.maybeHoldQuali(ampel).value("session").toObject().value("_quali_result").toBool());
    }

    void qualiErgebnisLaeuftNachHoldsAb()
    {
        Config cfg;
        SharedState sh(cfg);
        double now = 0.0;
        sh.setClock([&now]() { return now; });
        QJsonArray q{driver(1, 1, 30.0, 40.0, 90.0, 90.0)};
        sh.maybeHoldQuali(payload(true, q));
        QJsonObject rennen = payload(false, QJsonArray(), true, 0);
        QVERIFY(sh.maybeHoldQuali(rennen).value("session").toObject().value("_quali_result").toBool());
        now = 301 * 1000.0;                              // holds = 300 s
        QVERIFY(!sh.maybeHoldQuali(rennen).value("session").toObject().value("_quali_result").toBool());
    }

    void gapVerlaufProRunde()
    {
        Config cfg;
        SharedState sh(cfg);
        QJsonArray d1{driver(1, 1, 30.0, 40.0, 90.0), driver(2, 2, 30.0, 40.0, 91.0)};
        QJsonObject p = payload(false, d1, true, 3);
        QJsonArray drv = p.value("drivers").toArray();
        QJsonObject second = drv.at(1).toObject();
        second["gap_to_leader"] = 2.345;
        drv[1] = second;
        p["drivers"] = drv;
        sh.derive(p);
        QVERIFY(sh.gapHist().contains(3));
        QCOMPARE(sh.gapHist().value(3).value(2), 2.35);   // auf 1/100 gerundet
        QCOMPARE(sh.lapHist().value(3).value(1), 90.0);
    }
};

QTEST_GUILESS_MAIN(TstDerive)
#include "tst_derive.moc"
