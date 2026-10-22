// Hotlap-Karten (ab 0.3.1): Nachlauf nach der Linie und die F1-TV-Zielzeit.
//
// Gemeldet war: "sobald ein Fahrer eine Runde gefahren ist, verschwindet es
// sofort". Wer direkt die naechste fliegende Runde anhaengt, blieb auf Hotlap -
// der alte Nachlauf griff nur beim Abbiegen. Jetzt zaehlt das Ueberfahren der
// Linie (lap_num), die Karte steht 5 s mit der gefahrenen Zeit.
#include "derive.h"
#include "parts.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace kers::hud;

namespace {

/// Ein Fahrer auf fliegender Runde (driver_status 1) - Felder nach Bedarf ueberschreiben.
QJsonObject fahrer(int idx, const QJsonObject& extra = {})
{
    QJsonObject d{
        {"index", idx}, {"name", QStringLiteral("Fahrer %1").arg(idx)}, {"team", "Ferrari"},
        {"position", idx}, {"driver_status", 1}, {"in_pit", false}, {"lap_invalid", false},
        {"dnf", false}, {"dsq", false}, {"compound", "S"}, {"lap_num", 3}, {"lap_distance", 100.0},
        {"sector", 0}, {"sector1", 0.0}, {"sector2", 0.0}, {"current_lap_time", 10.0},
        {"last_lap", 0.0}, {"best_lap", 0.0}, {"ers_mode", 0}, {"ers_pct", 0.0},
    };
    for (auto it = extra.begin(); it != extra.end(); ++it)
        d[it.key()] = it.value();
    return d;
}

struct Welt {
    Config cfg;
    SharedState sh{cfg};
    Hotlaps h{sh, cfg};
    double uhr = 1000.0;
    Welt() { h.setClock([this]() { return uhr; }); }

    void stand(const QList<QJsonObject>& drivers, int elimCut = 0, int focus = -1)
    {
        h.update(drivers, focus, elimCut, true);
    }
    int anzahl() const { return static_cast<QAbstractItemModel*>(h.boxes())->rowCount(); }
    QVariant wert(int zeile, const char* rolle) const
    {
        auto* m = static_cast<QAbstractItemModel*>(h.boxes());
        const QHash<int, QByteArray> namen = m->roleNames();
        for (auto it = namen.cbegin(); it != namen.cend(); ++it)
            if (it.value() == rolle)
                return m->data(m->index(zeile, 0), it.key());
        qFatal("Rolle fehlt: %s", rolle);
        return {};
    }
};

/// Kurz vor der Linie: in S3, S1/S2 gefahren, Uhr bei 84.8 s.
QJsonObject vorDerLinie(int idx = 1)
{
    return fahrer(idx, {{"sector", 2}, {"sector1", 30.0}, {"sector2", 28.0},
                        {"current_lap_time", 84.8}, {"last_lap", 90.0}, {"lap_distance", 5200.0}});
}

}  // namespace

class TstHotlaps : public QObject {
    Q_OBJECT
private slots:
    void s3ErstNachDerLinie()
    {
        // In S3 sind S1/S2 schon die der laufenden Runde, last_lap die vorige -
        // das ist noch KEIN S3.
        QCOMPARE(s3Live(vorDerLinie()), 0.0);
        QJsonObject fertig = vorDerLinie();
        fertig["_fertig_zeit"] = 84.9;
        fertig["last_lap"] = 84.9;
        QVERIFY(qAbs(s3Live(fertig) - 26.9) < 1e-9);
    }

    void angehaengteRundeBleibtFuenfSekundenStehen()
    {
        Welt w;
        w.stand({vorDerLinie()});
        QCOMPARE(w.anzahl(), 1);
        QVERIFY(w.wert(0, "ticking").toBool());

        // Ueber die Linie und gleich weiter auf der naechsten fliegenden Runde.
        const QJsonObject neu = fahrer(1, {{"lap_num", 4}, {"current_lap_time", 0.3},
                                           {"last_lap", 84.9}, {"best_lap", 84.9}, {"position", 2}});
        w.uhr += 100;
        w.stand({neu});
        QCOMPARE(w.anzahl(), 1);
        QVERIFY(!w.wert(0, "ticking").toBool());
        QVERIFY(w.wert(0, "finished").toBool());
        QCOMPARE(w.wert(0, "staticTime").toDouble(), 84.9);
        QCOMPARE(w.wert(0, "position").toInt(), 2);                     // die NEUE Position
        const QVariantList sek = w.wert(0, "sectors").toList();
        QCOMPARE(sek.at(0).toDouble(), 30.0);                           // Sektoren der fertigen Runde
        QVERIFY(qAbs(sek.at(2).toDouble() - 26.9) < 1e-9);

        w.uhr += 4800;
        w.stand({fahrer(1, {{"lap_num", 4}, {"current_lap_time", 5.1}, {"last_lap", 84.9}})});
        QVERIFY(w.wert(0, "finished").toBool());

        w.uhr += 300;                                                   // 5,2 s nach der Linie
        w.stand({fahrer(1, {{"lap_num", 4}, {"current_lap_time", 5.4}, {"last_lap", 84.9}})});
        QCOMPARE(w.anzahl(), 1);
        QVERIFY(!w.wert(0, "finished").toBool());
        QVERIFY(w.wert(0, "ticking").toBool());                         // jetzt die neue Runde
    }

    void abbiegenNachDerLinieBleibtAuchStehen()
    {
        Welt w;
        w.stand({vorDerLinie()});
        w.uhr += 100;
        w.stand({fahrer(1, {{"lap_num", 4}, {"driver_status", 2}, {"current_lap_time", 0.3}, {"last_lap", 84.9}})});
        QCOMPARE(w.anzahl(), 1);
        QCOMPARE(w.wert(0, "staticTime").toDouble(), 84.9);
        w.uhr += 5100;
        w.stand({fahrer(1, {{"lap_num", 4}, {"driver_status", 2}, {"current_lap_time", 5.4}, {"last_lap", 84.9}})});
        QCOMPARE(w.anzahl(), 0);
    }

    void abgebrocheneRundeGehtSofort()
    {
        // Vom Hotlap gefallen, OHNE die Linie zu kreuzen -> keine "fertige" Zeit
        // (bis 0.3.0 stand hier die vorige Runde 90.0 als Ergebnis).
        Welt w;
        w.stand({fahrer(1, {{"current_lap_time", 40.0}, {"last_lap", 90.0}})});
        QCOMPARE(w.anzahl(), 1);
        w.uhr += 100;
        w.stand({fahrer(1, {{"driver_status", 2}, {"current_lap_time", 40.1}, {"last_lap", 90.0}})});
        QCOMPARE(w.anzahl(), 0);
    }

    void spaetesLastLapWirdNachgezogen()
    {
        Welt w;
        w.stand({vorDerLinie()});
        w.uhr += 100;
        // Rundenzaehler schon weiter, last_lap noch die alte Runde
        w.stand({fahrer(1, {{"lap_num", 4}, {"current_lap_time", 0.2}, {"last_lap", 90.0}})});
        QCOMPARE(w.wert(0, "staticTime").toDouble(), 84.8);             // vorlaeufig: letzte Uhr
        w.uhr += 100;
        w.stand({fahrer(1, {{"lap_num", 4}, {"current_lap_time", 0.3}, {"last_lap", 84.9}})});
        QCOMPARE(w.wert(0, "staticTime").toDouble(), 84.9);
    }

    void zielPoleOderCutLinie_mitZwischenzeit()
    {
        Welt w;
        // Q2 mit Cut nach Platz 2: A 80.0 (Pole), B 81.0 (Cut), C 82.0 (raus)
        const QJsonObject a = fahrer(1, {{"driver_status", 4}, {"best_lap", 80.0}, {"position", 1}});
        const QJsonObject b = fahrer(2, {{"driver_status", 4}, {"best_lap", 81.0}, {"position", 2},
                                         {"best_lap_sectors", QJsonArray{27.0, 27.0, 27.0}}});
        const QJsonObject c = fahrer(3, {{"best_lap", 82.0}, {"position", 3}});
        QJsonObject d = fahrer(4, {{"position", 4}});                    // noch ohne Zeit
        w.stand({a, b, c, d}, 2);
        // Karten: C und D (A und B sind nicht auf Hotlap: kein ERS, ds 4)
        QCOMPARE(w.anzahl(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(w.wert(i, "targetName").toString(), QStringLiteral("Fahrer 2"));
            QCOMPARE(w.wert(i, "targetTime").toDouble(), 27.0);         // Zwischenzeit S1
            QCOMPARE(w.wert(i, "targetSector").toInt(), 1);
        }
        d["sector1"] = 29.0;
        d["sector"] = 1;
        w.stand({a, b, c, d}, 2);
        const int zd = w.wert(0, "driverIndex").toInt() == 4 ? 0 : 1;
        QCOMPARE(w.wert(zd, "targetTime").toDouble(), 54.0);            // S1 + S2
        QCOMPARE(w.wert(zd, "targetSector").toInt(), 2);

        // Ohne Cut (Q3) geht es gegen die Pole, und zwar als Rundenzeit:
        // von A sind die Sektoren der Bestrunde nicht bekannt.
        Welt q3;
        q3.stand({a, b, c}, 0);
        QCOMPARE(q3.wert(0, "targetName").toString(), QStringLiteral("Fahrer 1"));
        QCOMPARE(q3.wert(0, "targetTime").toDouble(), 80.0);
        QCOMPARE(q3.wert(0, "targetSector").toInt(), 3);
    }

    void deltaNachDerLinie_gegenDasZielVonVorher()
    {
        Welt w;
        const QJsonObject a = fahrer(1, {{"driver_status", 4}, {"best_lap", 80.0}, {"position", 1}});
        const QJsonObject b = fahrer(2, {{"driver_status", 4}, {"best_lap", 81.0}, {"position", 2}});
        w.stand({a, b, vorDerLinie(4)}, 2);
        w.uhr += 100;
        // D faehrt 79.5: neue Pole. Das Delta zaehlt gegen B (Cut, vor der Linie),
        // die Runde ist lila.
        const QJsonObject d = fahrer(4, {{"lap_num", 4}, {"current_lap_time", 0.2}, {"last_lap", 79.5},
                                         {"best_lap", 79.5}, {"position", 1}});
        w.stand({d, a, b}, 2);
        QCOMPARE(w.anzahl(), 1);
        QVERIFY(w.wert(0, "finished").toBool());
        QVERIFY(w.wert(0, "hasFinal").toBool());
        QCOMPARE(w.wert(0, "targetName").toString(), QStringLiteral("Fahrer 2"));
        QVERIFY(qAbs(w.wert(0, "finalDelta").toDouble() - (79.5 - 81.0)) < 1e-9);
        QCOMPARE(w.wert(0, "lapClass").toString(), QStringLiteral("sp"));
    }

    void kameraFahrerIstMarkiert()
    {
        Welt w;
        w.stand({fahrer(1), fahrer(2, {{"lap_distance", 50.0}})}, 0, 2);
        QCOMPARE(w.wert(0, "driverIndex").toInt(), 2);                  // steht links
        QVERIFY(w.wert(0, "focused").toBool());
        QVERIFY(!w.wert(1, "focused").toBool());
    }
};

QTEST_GUILESS_MAIN(TstHotlaps)
#include "tst_hotlaps.moc"
