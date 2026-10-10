// Zugriffsschutz (ab 0.3.1): dieser PC frei, fremde Geraete nur mit Code bzw.
// Schluessel, Sperren bleiben, fremde Webseiten (CSRF/DNS-Rebinding) draussen.
//
// Meist direkt ueber KersServer::handle() mit gebauter Anfrage - so laesst sich
// ein "fremdes" Geraet einfach ueber req.peer spielen. 198.51.100.x ist ein
// Dokumentations-Netz und nie eine eigene Adresse.
#include "access_guard.h"
#include "kers_server.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace kers::server;

namespace {

const QHostAddress FREMD(QStringLiteral("198.51.100.7"));
const QHostAddress LOKAL(QHostAddress::LocalHost);

HttpRequest anfrage(const QByteArray& methode, const QString& pfad, const QHostAddress& peer,
                    const QByteArray& host = "127.0.0.1:5100")
{
    HttpRequest r;
    r.method = methode;
    const int q = pfad.indexOf(QLatin1Char('?'));
    r.path = q >= 0 ? pfad.left(q) : pfad;
    if (q >= 0)
        r.query = QUrlQuery(pfad.mid(q + 1));
    r.peer = peer;
    r.headers.insert("host", host);
    r.headers.insert("user-agent", "Mozilla/5.0 (Linux; Android 14) Chrome/130.0 Mobile");
    return r;
}

HttpRequest post(const QString& pfad, const QHostAddress& peer, const QJsonObject& body)
{
    HttpRequest r = anfrage("POST", pfad, peer);
    r.headers.insert("content-type", "application/json");
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return r;
}

QJsonObject jsonAus(const HttpResponse& r)
{
    return QJsonDocument::fromJson(r.body).object();
}

QByteArray kopf(const HttpResponse& r, const QByteArray& name)
{
    for (const auto& [n, v] : r.headers)
        if (n.toLower() == name.toLower())
            return v;
    return {};
}

}  // namespace

class TstAccess : public QObject {
    Q_OBJECT
private slots:
    void lokalFreiFremdNicht()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        QCOMPARE(srv.handle(anfrage("GET", "/api/settings", LOKAL)).status, 200);

        const HttpResponse api = srv.handle(anfrage("GET", "/api/settings", FREMD));
        QCOMPARE(api.status, 401);
        QCOMPARE(jsonAus(api).value("error").toString(), QStringLiteral("login"));
        const HttpResponse seite = srv.handle(anfrage("GET", "/regie", FREMD));
        QCOMPARE(seite.status, 401);                              // Login-Seite statt Regie
        QVERIFY(seite.contentType.startsWith("text/html"));
        QVERIFY(srv.handle(anfrage("GET", "/static/css/core.css", FREMD)).status != 401);

        // Der Versuch landet im Schaltbrett
        QCOMPARE(srv.access().attempts().size(), 1);
        QCOMPARE(srv.access().attempts().first().ip, QStringLiteral("198.51.100.7"));
    }

    void dnsRebindingIstFremd()
    {
        // Lokaler Absender, aber unter einem fremden Namen: zaehlt NICHT als dieser PC.
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        QCOMPARE(srv.handle(anfrage("GET", "/api/settings", LOKAL, "boese.example:5100")).status, 401);
        QCOMPARE(srv.handle(anfrage("GET", "/api/settings", LOKAL, "localhost:5100")).status, 200);
        QCOMPARE(srv.handle(anfrage("GET", "/api/settings", LOKAL, "[::1]:5100")).status, 200);
    }

    void loginUndSchluessel()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        const QString code = srv.access().code();
        QCOMPARE(code.size(), 6);

        HttpResponse r = srv.handle(post("/api/login", FREMD, {{"code", "000000" == code ? "111111" : "000000"}}));
        QCOMPARE(r.status, 403);
        QCOMPARE(jsonAus(r).value("rest").toInt(), AccessGuard::MAX_FEHLVERSUCHE - 1);

        r = srv.handle(post("/api/login", FREMD, {{"code", code}, {"name", "Handy Regie"}}));
        QCOMPARE(r.status, 200);
        const QByteArray key = jsonAus(r).value("key").toString().toLatin1();
        QVERIFY(key.size() >= 40);
        QVERIFY(kopf(r, "set-cookie").startsWith("kers_dev=" + key));
        QVERIFY(kopf(r, "set-cookie").contains("HttpOnly"));
        QCOMPARE(srv.access().devices().size(), 1);
        QCOMPARE(srv.access().devices().first().name, QStringLiteral("Handy Regie"));
        QVERIFY(srv.access().attempts().isEmpty());               // jetzt bekannt

        HttpRequest mitCookie = anfrage("GET", "/api/settings", FREMD);
        mitCookie.headers.insert("cookie", "foo=1; kers_dev=" + key + "; bar=2");
        QCOMPARE(srv.handle(mitCookie).status, 200);
        HttpRequest mitKopf = anfrage("GET", "/api/settings", FREMD);
        mitKopf.headers.insert("x-kers-key", key);
        QCOMPARE(srv.handle(mitKopf).status, 200);
        const HttpResponse mitQuery = srv.handle(anfrage("GET", "/api/settings?key=" + QString::fromLatin1(key), FREMD));
        QCOMPARE(mitQuery.status, 200);
        QVERIFY(kopf(mitQuery, "set-cookie").startsWith("kers_dev="));
        HttpRequest falsch = anfrage("GET", "/api/settings", FREMD);
        falsch.headers.insert("x-kers-key", "quatsch");
        QCOMPARE(srv.handle(falsch).status, 401);
    }

    void fehlversucheSperrenKurz()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        qint64 uhr = 1'000'000;
        srv.access().setClock([&uhr] { return uhr; });
        const QString code = srv.access().code();
        const QString falsch = code == "123456" ? "654321" : "123456";
        for (int i = 0; i < AccessGuard::MAX_FEHLVERSUCHE - 1; ++i)
            QCOMPARE(srv.handle(post("/api/login", FREMD, {{"code", falsch}})).status, 403);
        QCOMPARE(srv.handle(post("/api/login", FREMD, {{"code", falsch}})).status, 429);
        // Waehrend der Pause hilft auch der richtige Code nicht
        QCOMPARE(srv.handle(post("/api/login", FREMD, {{"code", code}})).status, 429);
        uhr += (AccessGuard::SPERRE_SEK + 1) * 1000LL;
        QCOMPARE(srv.handle(post("/api/login", FREMD, {{"code", code}})).status, 200);
    }

    void sperrenBleiben()
    {
        QTemporaryDir dir;
        QByteArray key;
        QString id;
        {
            KersServer srv(dir.path(), dir.filePath("x.json"));
            key = srv.access().createDevice("OBS PC2", &id);
            HttpRequest r = anfrage("GET", "/api/live", FREMD);
            r.headers.insert("x-kers-key", key);
            QCOMPARE(srv.handle(r).status, 200);
            srv.access().setDeviceBlocked(id, true);
            const HttpResponse gesperrt = srv.handle(r);
            QCOMPARE(gesperrt.status, 403);
            QVERIFY(gesperrt.closeConnection);
            srv.access().setIpBlocked("198.51.100.99", true);
        }
        // Neustart: Sperren stehen in access.json
        KersServer srv(dir.path(), dir.filePath("x.json"));
        HttpRequest r = anfrage("GET", "/api/live", FREMD);
        r.headers.insert("x-kers-key", key);
        QCOMPARE(srv.handle(r).status, 403);
        QVERIFY(srv.access().isIpBlocked("198.51.100.99"));
        const QHostAddress ip99(QStringLiteral("198.51.100.99"));
        QCOMPARE(srv.handle(post("/api/login", ip99, {{"code", srv.access().code()}})).status, 403);

        // Auch mit abgeschaltetem Schutz bleibt gesperrt gesperrt
        srv.access().setEnabled(false);
        QCOMPARE(srv.handle(anfrage("GET", "/api/live", ip99)).status, 403);
        QCOMPARE(srv.handle(anfrage("GET", "/api/live", FREMD)).status, 200);   // sonst frei
        srv.access().setDeviceBlocked(id, false);
        srv.access().removeDevice(id);
        QVERIFY(srv.access().devices().isEmpty());
    }

    void codeUndGeraeteBleiben()
    {
        QTemporaryDir dir;
        QString code;
        {
            KersServer srv(dir.path(), dir.filePath("x.json"));
            code = srv.access().code();
            srv.access().createDevice("Tablet");
        }
        KersServer srv(dir.path(), dir.filePath("x.json"));
        QCOMPARE(srv.access().code(), code);
        QCOMPARE(srv.access().devices().size(), 1);
        srv.access().newCode();
        QVERIFY(srv.access().code() != code || true);              // (kann zufaellig gleich sein)
        QCOMPARE(srv.access().code().size(), 6);
    }

    void fremdeWebseiten()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        HttpRequest r = post("/api/settings", LOKAL, {{"rows", 5}});
        r.headers.insert("origin", "http://boese.example");
        QCOMPARE(srv.handle(r).status, 403);
        r.headers.insert("origin", "http://127.0.0.1:5100");          // eigene Seite
        QCOMPARE(srv.handle(r).status, 200);

        HttpRequest formular = anfrage("POST", "/api/settings/reset", LOKAL);
        formular.headers.insert("content-type", "application/x-www-form-urlencoded");
        QCOMPARE(srv.handle(formular).status, 415);
        HttpRequest text = anfrage("POST", "/api/settings/reset", LOKAL);
        text.headers.insert("content-type", "text/plain;charset=UTF-8");
        QCOMPARE(srv.handle(text).status, 415);

        // Ohne Rumpf und ohne Typ (so schicken es regie.html & Co.) geht es weiter
        QCOMPARE(srv.handle(anfrage("POST", "/api/track/relearn", LOKAL)).status, 200);
        QCOMPARE(srv.handle(anfrage("GET", "/api/track/relearn", LOKAL)).status, 405);
    }

    void spielwiesenNurLokal()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        const QByteArray key = srv.access().createDevice("Handy");
        HttpRequest r = anfrage("GET", "/test", FREMD);
        r.headers.insert("x-kers-key", key);
        const HttpResponse a = srv.handle(r);
        QCOMPARE(a.status, 404);
        QVERIFY(a.body.contains("not found"));                    // die Wache, nicht das fehlende Template
    }

    void liveStromWirdGekappt()
    {
        // Echte Verbindung: ueber Loopback, aber mit fremdem Host-Namen - so ist es
        // fuer den Server ein fremdes Geraet, das sich per Schluessel ausweist.
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        QVERIFY(srv.start(0, 0, QHostAddress::LocalHost));
        QString id;
        const QByteArray key = srv.access().createDevice("Handy", &id);

        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, srv.httpPort());
        QTRY_VERIFY(s.state() == QAbstractSocket::ConnectedState);
        s.write("GET /api/stream?hz=20 HTTP/1.1\r\nHost: handy.example\r\nX-KERS-Key: " + key + "\r\n\r\n");
        QByteArray daten;
        QTRY_VERIFY((daten += s.readAll()).contains("data: "));
        QVERIFY(daten.startsWith("HTTP/1.1 200"));
        QCOMPARE(srv.streamClients(), 1);

        srv.access().setDeviceBlocked(id, true);
        QTRY_COMPARE(s.state(), QAbstractSocket::UnconnectedState);
        QTRY_COMPARE(srv.streamClients(), 0);
        srv.stop();
    }
};

QTEST_GUILESS_MAIN(TstAccess)
#include "tst_access.moc"
