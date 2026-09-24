// Der Quali-Status: wann HOTLAP, wann INLAP, wann OUTLAP.
// Port von Test\hud_pruefskripte\test_quali_status.py.
//
// Gemeldet war: "ab und zu steht INLAP, obwohl der Fahrer sich verbessert". Die
// Ursache steckt im Vergleichsmassstab: gemessen wird gegen die SUMME der
// persoenlichen Bestsektoren aus verschiedenen Runden - diese theoretische
// Bestzeit faehrt niemand je. Neu ist die Regel "ERS entlastet": wer deployt
// oder noch Ladung haelt, faehrt keine Auslaufrunde. Wenig ERS heisst dagegen
// weiterhin NIE inlap.
#include "derive.h"

#include <QJsonObject>
#include <QtTest>

using namespace kers::hud;

namespace {

/// Ein Fahrer auf der Strecke (driver_status 4 = on track).
QJsonObject fahrer(const QJsonObject& extra = {})
{
    QJsonObject d{
        {"index", 1}, {"driver_status", 4}, {"in_pit", false}, {"lap_invalid", false},
        {"sector1", 0.0}, {"sector2", 0.0}, {"last_lap", 0.0}, {"current_lap_time", 0.0},
        {"sector", 0}, {"ers_mode", 0}, {"ers_pct", 0.0},
    };
    for (auto it = extra.begin(); it != extra.end(); ++it)
        d[it.key()] = it.value();
    return d;
}

struct Welt {
    Config cfg;
    SharedState sh{cfg};
    explicit Welt(std::array<double, 3> best = {30.0, 40.0, 30.0})
    {
        sh.setDriverBestForTest(1, best);
    }
};

}  // namespace

class TstQualiStatus : public QObject {
    Q_OBJECT
private slots:
    void zuLangsamOhneErs_bleibtInlap()
    {
        Welt w;
        QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 32.0}, {"sector2", 40.0}})), QStringLiteral("inlap"));
    }

    void zuLangsamAberAmDeployen_track()
    {
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 32.0}, {"sector2", 40.0}, {"ers_mode", 2}})), QStringLiteral("track"));
        }
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 32.0}, {"sector2", 40.0}, {"ers_mode", 3}})), QStringLiteral("track"));
        }
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 32.0}, {"sector2", 40.0}, {"ers_pct", 55.0}})), QStringLiteral("track"));
        }
    }

    void wenigErsEntlastetNicht_undBelastetNicht()
    {
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 32.0}, {"sector2", 40.0}, {"ers_mode", 1}, {"ers_pct", 10.0}})),
                     QStringLiteral("inlap"));
        }
        {
            // ⚠ Der Kern der Meldung: 0 % ERS allein macht KEIN inlap. Ohne
            // Sektor-Beweis landet so ein Fahrer bei "out", nie bei "inlap".
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"ers_pct", 0.0}})), QStringLiteral("out"));
        }
    }

    void aufBestpaceBleibtHotlap_egalWievielErs()
    {
        const struct { double pct; int mode; } faelle[] = {{0.0, 0}, {100.0, 3}};
        for (const auto& f : faelle) {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"sector1", 30.2}, {"sector2", 40.0}, {"ers_pct", f.pct}, {"ers_mode", f.mode}})),
                     QStringLiteral("track"));
        }
    }

    void dieRegelnDavorSchlagenErs()
    {
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"lap_invalid", true}, {"ers_mode", 3}, {"ers_pct", 100.0}})), QStringLiteral("inlap"));
        }
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"in_pit", true}, {"ers_mode", 3}})), QStringLiteral("box"));
        }
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"driver_status", 3}, {"ers_mode", 3}})), QStringLiteral("out"));
        }
        {
            Welt w;
            QCOMPARE(w.sh.qualiStatus(fahrer({{"driver_status", 2}, {"ers_mode", 3}})), QStringLiteral("inlap"));
        }
    }

    void liveMittenImSektorZuLangsam_inlap()
    {
        // Noch kein Sektor fertig, aber die laufende Rundenzeit liegt schon
        // ueber Bestsektor 1 plus Toleranz -> zu langsam.
        Welt w;
        QCOMPARE(w.sh.qualiStatus(fahrer({{"current_lap_time", 32.0}, {"sector", 0}})), QStringLiteral("inlap"));
        // ... ausser er deployt gerade.
        QCOMPARE(w.sh.qualiStatus(fahrer({{"current_lap_time", 32.0}, {"sector", 0}, {"ers_mode", 2}})), QStringLiteral("track"));
    }
};

QTEST_GUILESS_MAIN(TstQualiStatus)
#include "tst_quali_status.moc"
