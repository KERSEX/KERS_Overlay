// Der eingebaute Server aus Sicht des Schaltbretts und des HUD:
// LocalServer (Start/Stop/Status), LocalFeed, LocalDataSource.
//
// Anlass: Nach dem Autostart zeigte das Schaltbrett "Start Server", weil es die
// einzige Statusmeldung verpasst hatte - und ein Klick darauf tat nichts, weil der
// Server schon lief. announce() muss den Stand deshalb IMMER melden.
#include "kers_server.h"
#include "local_link.h"
#include "server_control.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace kers;

class TstLocal : public QObject {
    Q_OBJECT
private slots:
    void statusNachAutostart()
    {
        QTemporaryDir dir;
        server::KersServer srv(dir.path(), dir.filePath("x.json"));
        app::LocalServer ctl(srv, 0, 0, QHostAddress::LocalHost);
        QVERIFY(srv.start(0, 0, QHostAddress::LocalHost));      // Autostart, bevor jemand zuhoert
        QCoreApplication::processEvents();

        // Das Schaltbrett meldet sich erst jetzt an ...
        QSignalSpy spy(&ctl, &app::ServerControl::statusChanged);
        ctl.announce();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);               // ... und erfaehrt: laeuft
        QCOMPARE(spy.at(0).at(1).toBool(), false);              // keine UDP-Daten

        // Klick auf "Start Server", obwohl er laeuft: trotzdem eine Meldung
        ctl.start();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), true);

        ctl.stop();                                              // (darf auch doppelt melden)
        QVERIFY(spy.count() >= 3);
        QCOMPARE(spy.last().at(0).toBool(), false);
        QVERIFY(!srv.isRunning());

        ctl.start();
        QVERIFY(srv.isRunning());
        QCOMPARE(spy.last().at(0).toBool(), true);
        srv.stop();
    }

    void startFehlerWirdGemeldet()
    {
        QTemporaryDir dir;
        server::KersServer erster(dir.path(), dir.filePath("x.json"));
        QVERIFY(erster.start(0, 0, QHostAddress::LocalHost));
        server::KersServer zweiter(dir.path(), dir.filePath("x.json"));
        app::LocalServer ctl(zweiter, erster.httpPort(), 0, QHostAddress::LocalHost);
        QSignalSpy fehler(&ctl, &app::ServerControl::errorOccurred);
        QSignalSpy status(&ctl, &app::ServerControl::statusChanged);
        ctl.start();
        QCOMPARE(fehler.count(), 1);
        QVERIFY(fehler.at(0).at(0).toString().contains(QStringLiteral("HTTP")));
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.at(0).at(0).toBool(), false);
        erster.stop();
    }

    void feedUndDatenquelle()
    {
        QTemporaryDir dir;
        server::KersServer srv(dir.path(), dir.filePath("x.json"));
        app::LocalFeed feed(&srv, 60);
        QSignalSpy link(&feed, &hud::Feed::linkChanged);
        QSignalSpy payload(&feed, &hud::Feed::payload);

        feed.start();                                            // Server aus: kein Link, keine Daten
        QTest::qWait(50);
        QCOMPARE(payload.count(), 0);

        QVERIFY(srv.start(0, 0, QHostAddress::LocalHost));
        QTRY_VERIFY(payload.count() >= 1);                       // erster Takt sofort
        QVERIFY(link.count() >= 1);
        QCOMPARE(link.last().at(0).toBool(), true);
        QVERIFY(payload.last().at(0).toJsonObject().contains("drivers"));

        app::LocalDataSource src(&srv);
        QJsonObject antwort;
        src.get("/api/track", [&](const QJsonObject& o) { antwort = o; });
        QTRY_VERIFY(antwort.contains("ver"));
        bool ok = false;
        src.post("/api/settings", {{"rows", 7}}, [&](bool o, const QJsonObject& a, const QString&) {
            ok = o && a.value("rows").toInt() == 7;
        });
        QTRY_VERIFY(ok);
        QCOMPARE(srv.settings().value("rows").toInt(), 7);

        srv.stop();
        QTRY_COMPARE(link.last().at(0).toBool(), false);
        bool gemeldet = false, erfolg = true;
        src.post("/api/settings", {{"rows", 3}}, [&](bool o, const QJsonObject&, const QString&) {
            gemeldet = true;
            erfolg = o;
        });
        QTRY_VERIFY(gemeldet);
        QVERIFY(!erfolg);                                        // Server aus -> Layout bleibt vorgemerkt
        feed.stop();
    }
};

QTEST_GUILESS_MAIN(TstLocal)
#include "tst_local.moc"
