// Der Server von aussen: HTTP ueber echte Sockets (Parser, Keep-Alive, Fehler),
// SSE, UDP-Empfang, Recorder und Kamera-Tasten (mit Attrappe statt SendInput).
#include "packet_builder.h"

#include "cam_keys.h"
#include "kers_server.h"
#include "recorder.h"
#include "sse_hub.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUdpSocket>

#include <QtZlib/zlib.h>

#include <cstring>

using namespace kers::server;
namespace T = kers::test;

namespace {

struct Antwort {
    int status = 0;
    QHash<QByteArray, QByteArray> kopf;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

/// Eine Antwort von einem offenen Socket lesen (Content-Length oder bis zum Schluss).
/// ⚠ Server und Test teilen sich einen Thread - deshalb qWait (Ereignisschleife
/// laeuft weiter) statt waitForReadyRead (die blockiert, und der Server antwortet nie).
Antwort lies(QTcpSocket& s, bool head = false, int timeoutMs = 3000)
{
    Antwort a;
    QByteArray buf;
    QElapsedTimer t;
    t.start();
    int kopfEnde = -1;
    qint64 laenge = -1;
    while (t.elapsed() < timeoutMs) {
        if (s.bytesAvailable() == 0)
            QTest::qWait(5);
        buf += s.readAll();
        if (kopfEnde < 0) {
            kopfEnde = buf.indexOf("\r\n\r\n");
            if (kopfEnde >= 0) {
                const QList<QByteArray> zeilen = buf.left(kopfEnde).split('\n');
                a.status = zeilen.value(0).split(' ').value(1).toInt();
                for (int i = 1; i < zeilen.size(); ++i) {
                    const int c = zeilen.at(i).indexOf(':');
                    if (c > 0)
                        a.kopf.insert(zeilen.at(i).left(c).trimmed().toLower(), zeilen.at(i).mid(c + 1).trimmed());
                }
                laenge = head ? 0 : a.kopf.value("content-length", "-1").toLongLong();
            }
        }
        if (kopfEnde >= 0 && laenge >= 0 && buf.size() >= kopfEnde + 4 + laenge) {
            a.body = buf.mid(kopfEnde + 4, laenge);
            return a;
        }
        if (s.state() != QAbstractSocket::ConnectedState && s.bytesAvailable() == 0)
            break;
    }
    if (kopfEnde >= 0)
        a.body = buf.mid(kopfEnde + 4);
    return a;
}

QByteArray gunzip(const QByteArray& in)
{
    z_stream z{};
    inflateInit2(&z, 15 + 32);
    QByteArray out;
    char buf[1 << 16];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    z.avail_in = static_cast<uInt>(in.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof(buf);
        rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, static_cast<int>(sizeof(buf) - z.avail_out));
    }
    inflateEnd(&z);
    return rc == Z_STREAM_END ? out : QByteArray();
}

}  // namespace

class TstServer : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    std::unique_ptr<KersServer> m_srv;

    QByteArray anfrage(const QByteArray& methode, const QByteArray& pfad, const QByteArray& body = {},
                       const QByteArray& extra = {})
    {
        QByteArray r = methode + ' ' + pfad + " HTTP/1.1\r\nHost: x\r\n" + extra;
        if (!body.isEmpty() || methode == "POST")
            r += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
        return r + "\r\n" + body;
    }

    Antwort einmal(const QByteArray& methode, const QByteArray& pfad, const QByteArray& body = {},
                   const QByteArray& extra = {})
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        if (!s.waitForConnected(2000))
            return {};
        s.write(anfrage(methode, pfad, body, extra));
        return lies(s, methode == "HEAD");
    }

private slots:
    void initTestCase()
    {
        m_srv = std::make_unique<KersServer>(m_dir.path(), m_dir.filePath("keine_tracks.json"));
        QVERIFY2(m_srv->start(0, 0, QHostAddress::LocalHost), qPrintable(m_srv->errorString()));
        QVERIFY(m_srv->httpPort() > 0);
        QVERIFY(m_srv->udpPort() > 0);
    }

    void cleanupTestCase()
    {
        m_srv->stop();
    }

    void apiUndKopf()
    {
        const Antwort a = einmal("GET", "/api/status");
        QCOMPARE(a.status, 200);
        QCOMPARE(a.kopf.value("content-type"), QByteArray("application/json"));
        QVERIFY(a.kopf.value("cache-control").contains("no-store"));
        QCOMPARE(a.json().value("connected").toBool(), false);
        QVERIFY(a.json().contains("driver_count"));
    }

    void keepAliveUndGestueckelt()
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s.waitForConnected(2000));
        s.write(anfrage("GET", "/api/regie"));
        QCOMPARE(lies(s).status, 200);
        // zweite Anfrage auf DERSELBEN Verbindung, Rumpf in zwei Stuecken
        const QByteArray body = R"({"chart":"lap"})";
        const QByteArray r = anfrage("POST", "/api/regie", body);
        s.write(r.left(r.size() - 5));
        s.flush();
        QTest::qWait(50);
        s.write(r.right(5));
        const Antwort a = lies(s);
        QCOMPARE(a.status, 200);
        QCOMPARE(a.json().value("chart").toString(), QStringLiteral("lap"));
        QCOMPARE(a.kopf.value("connection"), QByteArray("keep-alive"));
        s.write(anfrage("POST", "/api/regie", R"({"chart":"none"})"));
        QCOMPARE(lies(s).json().value("chart").toString(), QStringLiteral("none"));
    }

    void connectionClose()
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s.waitForConnected(2000));
        s.write("GET /api/status HTTP/1.1\r\nConnection: close\r\n\r\n");
        const Antwort a = lies(s);
        QCOMPARE(a.status, 200);
        QCOMPARE(a.kopf.value("connection"), QByteArray("close"));
        QTRY_COMPARE(s.state(), QAbstractSocket::UnconnectedState);
    }

    void fehlerfaelle()
    {
        QCOMPARE(einmal("GET", "/nix").status, 404);
        QCOMPARE(einmal("GET", "/part/nope").status, 404);
        QCOMPARE(einmal("GET", "/static/../main.py").status, 404);
        QCOMPARE(einmal("GET", "/static/%2e%2e/main.py").status, 404);
        const Antwort cam = einmal("GET", "/api/cam");
        QCOMPARE(cam.status, 405);
        QVERIFY(cam.kopf.value("allow").contains("POST"));
        const Antwort opt = einmal("OPTIONS", "/api/settings");
        QCOMPARE(opt.status, 200);
        QVERIFY(opt.kopf.value("allow").contains("GET"));

        // Kopf zu gross -> 431, Rumpf zu gross -> 413, Muell -> 400
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s.waitForConnected(2000));
        s.write("GET / HTTP/1.1\r\nX: " + QByteArray(20000, 'a'));
        QCOMPARE(lies(s).status, 431);
        QTcpSocket s2;
        s2.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s2.waitForConnected(2000));
        s2.write("POST /api/settings HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n");
        QCOMPARE(lies(s2).status, 413);
        QTcpSocket s3;
        s3.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s3.waitForConnected(2000));
        s3.write("KAPUTT\r\n\r\n");
        QCOMPARE(lies(s3).status, 400);
        // Kaputtes JSON ist wie {} (get_json(silent=True) or {})
        const Antwort j = einmal("POST", "/api/settings", "{kein json");
        QCOMPARE(j.status, 200);
        QCOMPARE(j.json().value("maprot").toInt(), 110);
    }

    void headUndSeiten()
    {
        const Antwort h = einmal("HEAD", "/api/status");
        QCOMPARE(h.status, 200);
        QVERIFY(h.kopf.value("content-length").toInt() > 0);
        QVERIFY(h.body.isEmpty());

        const Antwort p = einmal("GET", "/parts");
        QCOMPARE(p.status, 200);
        QVERIFY(p.kopf.value("content-type").startsWith("text/html"));
        QVERIFY(p.kopf.value("cache-control").contains("no-store"));
        QCOMPARE(p.body.count("class=\"card\""), 15);
        QVERIFY(!p.body.contains("{%"));
        QVERIFY(!p.body.contains("{{"));
        QVERIFY(p.body.contains("u-trackmap"));

        const Antwort css = einmal("GET", "/static/css/core.css");
        QCOMPARE(css.status, 200);
        QCOMPARE(css.kopf.value("content-type"), QByteArray("text/css; charset=utf-8"));
        const QByteArray etag = css.kopf.value("etag");
        QVERIFY(!etag.isEmpty());
        const Antwort nochmal = einmal("GET", "/static/css/core.css", {}, "If-None-Match: " + etag + "\r\n");
        QCOMPARE(nochmal.status, 304);
    }

    void settingsPresetsLayouts()
    {
        Antwort a = einmal("POST", "/api/settings", R"({"rows":"9","maprot":45})");
        QCOMPARE(a.json().value("rows").toInt(), 9);
        a = einmal("POST", "/api/presets", R"({"name":"  "})");
        QCOMPARE(a.status, 400);
        QCOMPARE(a.json().value("error").toString(), QStringLiteral("name fehlt"));
        a = einmal("POST", "/api/presets", R"({"name":"Rennen"})");
        QCOMPARE(a.json().value("presets").toObject().value("Rennen").toObject().value("rows").toInt(), 9);
        a = einmal("POST", "/api/presets", R"({"name":"Rennen","delete":true})");
        QVERIFY(a.json().value("presets").toObject().isEmpty());
        a = einmal("POST", "/api/layouts", R"({"name":"Leer"})");
        QVERIFY(a.json().value("layouts").toObject().contains("Leer"));
        a = einmal("GET", "/api/layout");
        QCOMPARE(a.json().value("teile").toArray().size(), 13);
        QCOMPARE(a.json().value("ecken").toArray().size(), 9);
        a = einmal("POST", "/api/settings/reset");
        QCOMPARE(a.json().value("rows").toInt(), 0);
        QCOMPARE(einmal("GET", "/api/settings/reset").status, 405);
    }

    void udpUndSse()
    {
        // Ein Stream mit 20 Hz und slim
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_srv->httpPort());
        QVERIFY(s.waitForConnected(2000));
        s.write("GET /api/stream?hz=20&slim=1 HTTP/1.1\r\nHost: x\r\n\r\n");
        QTRY_COMPARE(m_srv->streamClients(), 1);

        // UDP-Pakete an den Server
        QSignalSpy spy(m_srv.get(), &KersServer::packetReceived);
        QUdpSocket u;
        T::Pkt part(4, T::SIZE_PARTICIPANTS);
        for (int i = 0; i < 24; ++i)
            part.u16(T::HDR + 1 + i * 60 + 5, i < 2 ? 1 : 65535);
        u.writeDatagram(part, QHostAddress::LocalHost, m_srv->udpPort());
        u.writeDatagram(QByteArray(5, 'x'), QHostAddress::LocalHost, m_srv->udpPort());   // kein Header
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(m_srv->status().value("driver_count").toInt(), 2);

        QByteArray daten;
        QTRY_VERIFY_WITH_TIMEOUT((daten += s.readAll(), daten.count("data: ") >= 3), 3000);
        QVERIFY(daten.startsWith("HTTP/1.1 200 OK"));
        QVERIFY(daten.contains("Content-Type: text/event-stream"));
        QVERIFY(!daten.contains("Content-Length"));
        const QList<QByteArray> events = daten.split('\n');
        QList<QJsonObject> objs;
        for (const QByteArray& l : events)
            if (l.startsWith("data: "))
                objs << QJsonDocument::fromJson(l.mid(6)).object();
        QVERIFY(objs.size() >= 3);
        QVERIFY(objs.at(0).contains("settings"));             // erster Takt: alles
        QVERIFY(!objs.at(1).contains("settings"));            // danach nur noch bei Aenderung
        QVERIFY(objs.at(1).contains("drivers"));

        s.disconnectFromHost();
        QTRY_COMPARE(m_srv->streamClients(), 0);
    }

    void tickFuerHz()
    {
        QCOMPARE(SseHub::tickFor(""), 0.08);
        QCOMPARE(SseHub::tickFor("abc"), 0.08);
        QCOMPARE(SseHub::tickFor("0"), 0.08);
        QCOMPARE(SseHub::tickFor("30"), 1.0 / 30);
        QCOMPARE(SseHub::tickFor("500"), 1.0 / 60);
        QCOMPARE(SseHub::tickFor("0.5"), 1.0);
    }

    void portBelegt()
    {
        KersServer zweiter(m_dir.path(), m_dir.filePath("x.json"));
        QVERIFY(!zweiter.start(m_srv->httpPort(), 0, QHostAddress::LocalHost));
        QVERIFY(zweiter.errorString().contains(QStringLiteral("HTTP")));
        KersServer dritter(m_dir.path(), m_dir.filePath("x.json"));
        QVERIFY(!dritter.start(0, m_srv->udpPort(), QHostAddress::LocalHost));
        QVERIFY(dritter.errorString().contains(QStringLiteral("UDP")));
    }

    void recorder()
    {
        Antwort a = einmal("POST", "/api/record/start", R"({"name":"Test Lauf/..","only_parsed":true})");
        QVERIFY(a.json().value("ok").toBool());
        const QString datei = a.json().value("file").toString();
        QVERIFY(datei.endsWith(QStringLiteral("_Test_Lauf.f1rec")));
        QCOMPARE(einmal("POST", "/api/record/start").json().value("ok").toBool(), false);
        m_srv->feedPacket(T::Pkt(2, T::SIZE_LAPDATA));
        m_srv->feedPacket(T::Pkt(11, T::SIZE_HISTORY));                   // PID 11: nicht in only_parsed
        m_srv->feedPacket(T::event("CHQF"));
        a = einmal("GET", "/api/record/status");
        QVERIFY(a.json().value("active").toBool());
        QCOMPARE(a.json().value("count").toInt(), 2);
        a = einmal("POST", "/api/record/stop");
        QCOMPARE(a.json().value("count").toInt(), 2);
        QCOMPARE(einmal("POST", "/api/record/stop").json().value("err").toString(), QStringLiteral(u"läuft nicht"));

        QFile f(m_dir.filePath("recordings/" + datei));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray roh = gunzip(f.readAll());
        const int nl = roh.indexOf('\n');
        const QJsonObject meta = QJsonDocument::fromJson(roh.left(nl)).object();
        QCOMPARE(meta.value("format").toInt(), 2026);
        QCOMPARE(meta.value("only_parsed").toBool(), true);
        QCOMPARE(meta.value("note").toString(), QStringLiteral("Test Lauf/.."));
        int pos = nl + 1, n = 0;
        QList<int> pids;
        while (pos + 12 <= roh.size()) {
            quint32 len;
            std::memcpy(&len, roh.constData() + pos + 8, 4);
            pids << static_cast<uint8_t>(roh.at(pos + 12 + 6));
            pos += 12 + static_cast<int>(len);
            ++n;
        }
        QCOMPARE(pos, roh.size());
        QCOMPARE(pids, (QList<int>{2, 3}));
    }

    void kameraTasten()
    {
        CamKeys cam;
        QList<QString> log;
        QElapsedTimer uhr;
        uhr.start();
        cam.setSender([&](quint16 scan, bool ext, bool up) {
            log << QString(up ? QStringLiteral("-") : QStringLiteral("+")) + QString::number(scan, 16)
                       + (ext ? QStringLiteral("e") : QString()) + QLatin1Char('@') + QString::number(uhr.elapsed() / 10);
            return true;
        });
        QCOMPARE(CamKeys::folgeFuer(3, "f8"), QStringList{"3"});
        QCOMPARE(CamKeys::folgeFuer(10, "f8"), QStringList{"0"});
        QCOMPARE(CamKeys::folgeFuer(12, "f8"), QStringList{"shift+2"});
        QCOMPARE(CamKeys::folgeFuer(20, "f8"), QStringList{"shift+0"});
        QCOMPARE(CamKeys::folgeFuer(22, "f8"), (QStringList{"shift+0", "f8", "f8"}));
        QVERIFY(CamKeys::folgeFuer(21, "").isEmpty());
        QVERIFY(CamKeys::folgeFuer(23, "f8").isEmpty());

        quint16 sc;
        bool ext;
        QVERIFY(CamKeys::scancode("f8", sc, ext) && sc == 0x42 && !ext);
        QVERIFY(CamKeys::scancode("3", sc, ext) && sc == 0x04);
        QVERIFY(CamKeys::scancode("shift", sc, ext) && sc == 0x2a);
        QVERIFY(CamKeys::scancode("left", sc, ext) && sc == 0x4b && ext);
        QVERIFY(!CamKeys::scancode("win", sc, ext));

        QVERIFY(cam.druecke({"shift+2"}, 50).isEmpty());
        QTRY_VERIFY(!cam.busy());
        // Shift runter, 20 ms, 2 runter, 50 ms halten, 2 hoch, Shift hoch
        QCOMPARE(log.size(), 4);
        QVERIFY(log.at(0).startsWith("+2a"));
        QVERIFY(log.at(1).startsWith("+3@"));
        QVERIFY(log.at(2).startsWith("-3@"));
        QVERIFY(log.at(3).startsWith("-2a"));
        const int runter = log.at(1).section('@', 1).toInt(), hoch = log.at(2).section('@', 1).toInt();
        QVERIFY2(hoch - runter >= 4, "Taste wurde nicht gehalten");

        log.clear();
        QVERIFY(!cam.druecke({"win+r"}, 50).isEmpty());          // nicht erlaubt -> nichts gesendet
        QVERIFY(log.isEmpty());

        // Ueber HTTP: Kamera-Tasten aus -> 403, P21 ohne Weiter-Taste -> 400
        einmal("POST", "/api/settings", R"({"camkeys":false})");
        QCOMPARE(einmal("POST", "/api/cam", R"({"pos":3})").status, 403);
        einmal("POST", "/api/settings", R"({"camkeys":true,"camnextkey":""})");
        QCOMPARE(einmal("POST", "/api/cam", R"({"pos":21})").status, 400);
        QCOMPARE(einmal("POST", "/api/cam", R"({"pos":0})").status, 400);
        QCOMPARE(einmal("POST", "/api/cam", R"({})").status, 400);
        m_srv->camKeys().setSender([](quint16, bool, bool) { return true; });   // nichts ins echte Fenster
        const Antwort ok = einmal("POST", "/api/cam", R"({"pos":"12"})");
        QCOMPARE(ok.status, 200);
        QCOMPARE(ok.json().value("keys").toArray(), QJsonArray{"shift+2"});
        QCOMPARE(ok.json().value("hold").toInt(), 60);
        einmal("POST", "/api/settings/reset");
    }

    void stopUndStart()
    {
        const quint16 port = m_srv->httpPort();
        QSignalSpy spy(m_srv.get(), &KersServer::runningChanged);
        m_srv->stop();
        QVERIFY(!m_srv->isRunning());
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(!s.waitForConnected(500));
        QVERIFY(m_srv->start(port, 0, QHostAddress::LocalHost));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(einmal("GET", "/api/status").status, 200);
    }
};

QTEST_GUILESS_MAIN(TstServer)
#include "tst_server.moc"
