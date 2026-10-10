// OBS-Steuerung (ab 0.3.1) gegen ein nachgebautes OBS: obs-websocket v5 mit
// Hello/Identify (mit und ohne Passwort), Zustand laden, Events, Aktionen aus der
// festen Liste, DPAPI, Browser-Docks aus der OBS-Konfiguration, Routen.
#include "kers_server.h"
#include "obs_link.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>

using namespace kers::server;

namespace {

/// Ein OBS mit drei Szenen, zwei Quellen, einem Mikro und zwei Uebergaengen.
class FakeObs : public QObject {
public:
    QWebSocketServer srv{QStringLiteral("fake-obs"), QWebSocketServer::NonSecureMode};
    QWebSocket* client = nullptr;
    QString password;                                 // leer = ohne Anmeldung
    const QString salt = QStringLiteral("c2FsdA=="), challenge = QStringLiteral("Y2hhbGxlbmdl");
    QList<QJsonObject> requests;
    QString scene = QStringLiteral("Rennen");

    quint16 start()
    {
        srv.listen(QHostAddress::LocalHost, 0);
        connect(&srv, &QWebSocketServer::newConnection, this, [this] {
            client = srv.nextPendingConnection();
            connect(client, &QWebSocket::textMessageReceived, this, [this](const QString& t) { handle(t); });
            QJsonObject hello{{"obsWebSocketVersion", "5.5.0"}, {"rpcVersion", 1}};
            if (!password.isEmpty())
                hello["authentication"] = QJsonObject{{"challenge", challenge}, {"salt", salt}};
            send(0, hello);
        });
        return srv.serverPort();
    }
    void send(int op, const QJsonObject& d)
    {
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op", op}, {"d", d}}).toJson()));
    }
    void event(const QString& type, const QJsonObject& data)
    {
        send(5, {{"eventType", type}, {"eventIntent", 1}, {"eventData", data}});
    }
    QJsonObject last() const { return requests.isEmpty() ? QJsonObject() : requests.last(); }

    void handle(const QString& text)
    {
        const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
        const QJsonObject d = o.value("d").toObject();
        if (o.value("op").toInt() == 1) {
            if (!password.isEmpty() && d.value("authentication").toString() != ObsLink::authString(password, salt, challenge)) {
                client->close(static_cast<QWebSocketProtocol::CloseCode>(4009), QStringLiteral("Authentication failed."));
                return;
            }
            send(2, {{"negotiatedRpcVersion", 1}});
            return;
        }
        if (o.value("op").toInt() != 6)
            return;
        requests.append(d);
        const QString typ = d.value("requestType").toString();
        const QJsonObject rd = d.value("requestData").toObject();
        bool ok = true;
        QJsonObject antwort;
        if (typ == "GetSceneList")
            antwort = {{"currentProgramSceneName", scene}, {"currentPreviewSceneName", ""},
                       {"scenes", QJsonArray{QJsonObject{{"sceneIndex", 0}, {"sceneName", "Pause"}},
                                             QJsonObject{{"sceneIndex", 1}, {"sceneName", "Rennen"}},
                                             QJsonObject{{"sceneIndex", 2}, {"sceneName", "Start"}}}}};
        else if (typ == "GetSceneItemList")
            antwort = {{"sceneItems", QJsonArray{QJsonObject{{"sceneItemId", 1}, {"sourceName", "Spiel"}, {"sceneItemEnabled", true}, {"sceneItemIndex", 0}},
                                                 QJsonObject{{"sceneItemId", 2}, {"sourceName", "KERS"}, {"sceneItemEnabled", true}, {"sceneItemIndex", 1}}}}};
        else if (typ == "GetInputList")
            antwort = {{"inputs", QJsonArray{QJsonObject{{"inputName", "Mic"}, {"inputKind", "wasapi_input_capture"}},
                                             QJsonObject{{"inputName", "Spiel"}, {"inputKind", "game_capture"}}}}};
        else if (typ == "GetInputMute")
            ok = rd.value("inputName").toString() == "Mic", antwort = {{"inputMuted", false}};
        else if (typ == "GetInputVolume")
            antwort = {{"inputVolumeDb", -6.0}, {"inputVolumeMul", 0.5}};
        else if (typ == "GetSceneTransitionList")
            antwort = {{"currentSceneTransitionName", "Fade"},
                       {"transitions", QJsonArray{QJsonObject{{"transitionName", "Fade"}}, QJsonObject{{"transitionName", "Cut"}}}}};
        else if (typ == "GetStudioModeEnabled")
            antwort = {{"studioModeEnabled", false}};
        else if (typ == "GetVersion")
            antwort = {{"obsVersion", "31.0.0"}};
        else if (typ == "GetStreamStatus")
            antwort = {{"outputActive", false}, {"outputReconnecting", false}, {"outputTimecode", "00:00:00.000"}};
        else if (typ == "GetRecordStatus")
            antwort = {{"outputActive", true}, {"outputPaused", false}, {"outputTimecode", "00:01:02.345"}};
        QJsonObject r{{"requestType", typ}, {"requestId", d.value("requestId")},
                      {"requestStatus", QJsonObject{{"result", ok}, {"code", ok ? 100 : 600}}}};
        if (!antwort.isEmpty())
            r["responseData"] = antwort;
        send(7, r);
    }
};

QString obsJson(const QTemporaryDir& dir, quint16 port, const QString& pw = QString())
{
    const QString f = dir.filePath("obs.json");
    ObsLink tmp(f);                                   // schreibt die Datei samt DPAPI-Passwort
    ObsLink::Config c;
    c.enabled = true;
    c.host = QStringLiteral("127.0.0.1");
    c.port = port;
    c.password = pw;
    tmp.setConfig(c, true);
    return f;
}

}  // namespace

class TstObs : public QObject {
    Q_OBJECT
private slots:
    void verbindenUndZustand()
    {
        QTemporaryDir dir;
        FakeObs obs;
        ObsLink link(obsJson(dir, obs.start()));
        link.start();
        QTRY_VERIFY(link.isConnected());
        QTRY_COMPARE(link.state().value("transitions").toArray().size(), 2);
        QTRY_COMPARE(link.state().value("inputs").toArray().size(), 1);   // nur Mic hat Ton
        const QJsonObject s = link.state();
        QCOMPARE(s.value("scenes").toArray().first().toString(), QStringLiteral("Start"));   // oben = hoechster Index
        QCOMPARE(s.value("program").toString(), QStringLiteral("Rennen"));
        QTRY_COMPARE(link.state().value("items").toArray().size(), 2);
        QCOMPARE(link.state().value("items").toArray().first().toObject().value("name").toString(), QStringLiteral("KERS"));
        QTRY_COMPARE(link.state().value("inputs").toArray().first().toObject().value("db").toDouble(), -6.0);
        QTRY_COMPARE(link.state().value("record").toObject().value("timecode").toString(), QStringLiteral("00:01:02"));
        QCOMPARE(link.state().value("error").toString(), QString());
    }

    void passwortRichtigFalschFehlt()
    {
        {
            QTemporaryDir dir;
            FakeObs obs;
            obs.password = QStringLiteral("geheim");
            ObsLink link(obsJson(dir, obs.start(), QStringLiteral("geheim")));
            link.start();
            QTRY_VERIFY(link.isConnected());
        }
        {
            QTemporaryDir dir;
            FakeObs obs;
            obs.password = QStringLiteral("geheim");
            ObsLink link(obsJson(dir, obs.start(), QStringLiteral("falsch")));
            link.start();
            QTRY_VERIFY(link.state().value("error").toString().contains(QStringLiteral("lehnt das Passwort ab")));
            QVERIFY(!link.state().value("error").toString().contains(QStringLiteral("nicht erreichbar")));
            QVERIFY(!link.isConnected());
        }
        {
            QTemporaryDir dir;
            FakeObs obs;
            obs.password = QStringLiteral("geheim");
            ObsLink link(obsJson(dir, obs.start()));
            link.start();
            QTRY_VERIFY(link.state().value("error").toString().contains(QStringLiteral("verlangt ein Passwort")));
        }
    }

    void aktionenNurAusDerListe()
    {
        QTemporaryDir dir;
        FakeObs obs;
        ObsLink link(obsJson(dir, obs.start()));
        QCOMPARE(link.action({{"action", "scene"}, {"name", "Start"}}), QStringLiteral("OBS ist nicht verbunden"));
        link.start();
        QTRY_VERIFY(link.isConnected());
        QTRY_COMPARE(link.state().value("items").toArray().size(), 2);
        QTRY_COMPARE(link.state().value("inputs").toArray().size(), 1);

        QCOMPARE(link.action({{"action", "scene"}, {"name", "Start"}}), QString());
        QTRY_COMPARE(obs.last().value("requestType").toString(), QStringLiteral("SetCurrentProgramScene"));
        QCOMPARE(obs.last().value("requestData").toObject().value("sceneName").toString(), QStringLiteral("Start"));
        QVERIFY(!link.action({{"action", "scene"}, {"name", "Gibtsnicht"}}).isEmpty());

        QCOMPARE(link.action({{"action", "item"}, {"id", 2}, {"enabled", false}}), QString());
        QTRY_COMPARE(obs.last().value("requestType").toString(), QStringLiteral("SetSceneItemEnabled"));
        QCOMPARE(obs.last().value("requestData").toObject().value("sceneName").toString(), QStringLiteral("Rennen"));
        QVERIFY(!link.action({{"action", "item"}, {"id", 99}, {"enabled", false}}).isEmpty());

        QCOMPARE(link.action({{"action", "volume"}, {"input", "Mic"}, {"db", 50}}), QString());
        QTRY_COMPARE(obs.last().value("requestType").toString(), QStringLiteral("SetInputVolume"));
        QCOMPARE(obs.last().value("requestData").toObject().value("inputVolumeDb").toDouble(), 26.0);   // begrenzt
        QVERIFY(!link.action({{"action", "mute"}, {"input", "Spiel"}, {"muted", true}}).isEmpty());   // kein Ton

        QCOMPARE(link.action({{"action", "record"}, {"op", "pause"}}), QString());
        QTRY_COMPARE(obs.last().value("requestType").toString(), QStringLiteral("PauseRecord"));
        QCOMPARE(link.action({{"action", "stream"}, {"op", "start"}}), QString());
        QTRY_COMPARE(obs.last().value("requestType").toString(), QStringLiteral("StartStream"));
        QVERIFY(!link.action({{"action", "stream"}, {"op", "explode"}}).isEmpty());
        QVERIFY(!link.action({{"action", "Shutdown"}}).isEmpty());          // nichts durchreichen
        QVERIFY(!link.action({{"action", "preview"}, {"name", "Start"}}).isEmpty());   // Studio-Modus aus
    }

    void eventsAktualisieren()
    {
        QTemporaryDir dir;
        FakeObs obs;
        ObsLink link(obsJson(dir, obs.start()));
        link.start();
        QTRY_COMPARE(link.state().value("inputs").toArray().size(), 1);
        QSignalSpy spy(&link, &ObsLink::stateChanged);
        obs.scene = QStringLiteral("Start");
        obs.event("CurrentProgramSceneChanged", {{"sceneName", "Start"}});
        QTRY_COMPARE(link.state().value("program").toString(), QStringLiteral("Start"));
        obs.event("InputMuteStateChanged", {{"inputName", "Mic"}, {"inputMuted", true}});
        QTRY_VERIFY(link.state().value("inputs").toArray().first().toObject().value("muted").toBool());
        obs.event("CurrentSceneTransitionChanged", {{"transitionName", "Cut"}});
        QTRY_COMPARE(link.state().value("transition").toString(), QStringLiteral("Cut"));
        QTRY_VERIFY(spy.count() >= 1);
    }

    void passwortVerschluesselt()
    {
        QCOMPARE(ObsLink::unprotect(ObsLink::protect("Spa\xc3\x9f mit OBS")), QByteArray("Spa\xc3\x9f mit OBS"));
        QTemporaryDir dir;
        const QString f = obsJson(dir, 4455, QStringLiteral("klartext-pw"));
        QFile datei(f);
        QVERIFY(datei.open(QIODevice::ReadOnly));
        QVERIFY(!datei.readAll().contains("klartext-pw"));
        ObsLink wieder(f);
        QCOMPARE(wieder.config().password, QStringLiteral("klartext-pw"));
        QCOMPARE(wieder.configJson().value("has_password").toBool(), true);
        QVERIFY(!wieder.configJson().contains("password"));
    }

    void browserDocks()
    {
        QTemporaryDir dir;
        QFile ini(dir.filePath("user.ini"));
        QVERIFY(ini.open(QIODevice::WriteOnly));
        ini.write("[General]\r\nFoo=1\r\n[BasicWindow]\r\n"
                  "ExtraBrowserDocks=[{\"title\": \"ChatKERS\", \"url\": \"https://www.twitch.tv/popout/who_kers/chat?popout=\", \"uuid\": \"a\"}, "
                  "{\"title\": \"Alerts\", \"url\": \"https://example.org/alerts\", \"uuid\": \"b\"}, "
                  "{\"title\": \"Boese\", \"url\": \"javascript:alert(1)\", \"uuid\": \"c\"}]\r\n");
        ini.close();
        const QJsonArray d = ObsLink::browserDocks(dir.path());
        QCOMPARE(d.size(), 2);
        QCOMPARE(d.at(0).toObject().value("twitch").toString(), QStringLiteral("who_kers"));
        QVERIFY(d.at(1).toObject().value("twitch").isNull());
        QCOMPARE(ObsLink::twitchChannel("https://www.twitch.tv/embed/German_Formula1_League/chat?parent=x"),
                 QStringLiteral("german_formula1_league"));
    }

    void routen()
    {
        QTemporaryDir dir;
        KersServer srv(dir.path(), dir.filePath("x.json"));
        auto anfrage = [](const QByteArray& m, const QString& p, const QHostAddress& peer) {
            HttpRequest r;
            r.method = m;
            r.path = p;
            r.peer = peer;
            r.headers.insert("host", "127.0.0.1:5100");
            r.headers.insert("content-type", "application/json");
            return r;
        };
        QCOMPARE(srv.handle(anfrage("GET", "/api/obs", QHostAddress::LocalHost)).status, 200);
        // Ein freigegebenes Handy darf OBS steuern, aber nicht umkonfigurieren
        const QByteArray key = srv.access().createDevice("Handy");
        HttpRequest cfg = anfrage("POST", "/api/obs/config", QHostAddress(QStringLiteral("198.51.100.7")));
        cfg.headers.insert("x-kers-key", key);
        cfg.body = R"({"host":"198.51.100.66"})";
        QCOMPARE(srv.handle(cfg).status, 403);
        HttpRequest lokal = anfrage("POST", "/api/obs/config", QHostAddress::LocalHost);
        lokal.body = R"({"enabled":false,"password":"pw"})";
        const HttpResponse r = srv.handle(lokal);
        QCOMPARE(r.status, 200);
        const QJsonObject j = QJsonDocument::fromJson(r.body).object();
        QCOMPARE(j.value("has_password").toBool(), true);
        QVERIFY(!j.contains("password"));
        QCOMPARE(j.value("enabled").toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TstObs)
#include "tst_obs.moc"
