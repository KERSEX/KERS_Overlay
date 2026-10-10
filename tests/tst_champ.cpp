// WM-Stand ab 0.3.1: eigene Punkteliste ("points_system"), Team-WM ("teams"),
// Speichern der Liste aus /settings - und alte Dateien rechnen wie bisher.
#include "packet_builder.h"

#include "kers_server.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace kers::server;
using kers::test::HDR;
using kers::test::Pkt;
namespace T = kers::test;

namespace {

// Drei Autos: Verstappen P2, Leclerc P1, Norris P3
Pkt teilnehmer()
{
    Pkt p(4, T::SIZE_PARTICIPANTS);
    const int base = HDR + 1;
    p.u8(HDR, 3);
    for (int i = 0; i < 24; ++i)
        p.u16(base + i * 60 + 5, 65535);
    p.u16(base + 5, 1).u8(base + 8, 1).str(base + 10, "Verstappen");
    p.u16(base + 60 + 5, 2).u8(base + 60 + 8, 16).str(base + 60 + 10, "Leclerc");
    p.u16(base + 120 + 5, 3).u8(base + 120 + 8, 4).str(base + 120 + 10, "Norris");
    return p;
}

Pkt runden()
{
    Pkt p(2, T::SIZE_LAPDATA);
    const int pos[] = {2, 1, 3};
    for (int i = 0; i < 3; ++i) {
        const int s = HDR + i * 57;
        p.f32(s + 20, 1000.0f).u8(s + 32, pos[i]).u8(s + 33, 3).u8(s + 44, 4).u8(s + 45, 2);
    }
    return p;
}

QJsonObject wmDatei(const QJsonObject& extra = {})
{
    QJsonObject o{{"title", "Test-WM"},
                  {"_comment", "bleibt erhalten"},
                  {"drivers", QJsonArray{
                                  QJsonObject{{"name", "Max"}, {"team", "Red Bull"}, {"base_points", 100}, {"in_game_name", "Verstappen"}},
                                  QJsonObject{{"name", "Charles"}, {"team", "Ferrari"}, {"base_points", 90}, {"in_game_name", "Leclerc"}},
                                  QJsonObject{{"name", "Lando"}, {"team", "Red Bull"}, {"base_points", 50}, {"in_game_name", "Norris"}},
                              }}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o[it.key()] = it.value();
    return o;
}

struct Fix {
    QTemporaryDir dir;
    std::unique_ptr<KersServer> srv;
    explicit Fix(const QJsonObject& datei)
    {
        QFile f(dir.filePath("championship.json"));
        if (!f.open(QIODevice::WriteOnly))
            qFatal("WM-Datei nicht schreibbar");
        f.write(QJsonDocument(datei).toJson());
        f.close();
        srv = std::make_unique<KersServer>(dir.path(), dir.filePath("x.json"));
        srv->feedPacket(teilnehmer());
        srv->feedPacket(runden());
    }
    QJsonObject fahrer(const QString& name) const
    {
        for (const QJsonValue& v : srv->championship().value("standings").toArray())
            if (v.toObject().value("name").toString() == name)
                return v.toObject();
        return {};
    }
    QJsonObject team(const QString& name) const
    {
        for (const QJsonValue& v : srv->championship().value("teams").toArray())
            if (v.toObject().value("team").toString() == name)
                return v.toObject();
        return {};
    }
};

HttpRequest post(const QString& pfad, const QJsonObject& body)
{
    HttpRequest r;
    r.method = "POST";
    r.path = pfad;
    r.peer = QHostAddress::LocalHost;
    r.headers.insert("host", "127.0.0.1:5100");
    r.headers.insert("content-type", "application/json");
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return r;
}

}  // namespace

class TstChamp : public QObject {
    Q_OBJECT
private slots:
    void standardWieBisher()
    {
        Fix f(wmDatei());
        QCOMPARE(f.fahrer("Max").value("live_points").toDouble(), 18.0);
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 25.0);
        QCOMPARE(f.fahrer("Lando").value("live_points").toDouble(), 15.0);
        QCOMPARE(f.fahrer("Max").value("total_points").toDouble(), 118.0);
        const QJsonObject c = f.srv->championship();
        QCOMPARE(c.value("custom_points").toBool(), false);
        QCOMPARE(c.value("points_system").toObject().value("1").toDouble(), 25.0);
        QCOMPARE(c.value("standings").toArray().first().toObject().value("name").toString(), QStringLiteral("Max"));
    }

    void teamWertung()
    {
        Fix f(wmDatei());
        const QJsonArray teams = f.srv->championship().value("teams").toArray();
        QCOMPARE(teams.size(), 2);
        QCOMPARE(teams.at(0).toObject().value("team").toString(), QStringLiteral("Red Bull"));
        QCOMPARE(f.team("Red Bull").value("base_points").toDouble(), 150.0);
        QCOMPARE(f.team("Red Bull").value("live_points").toDouble(), 33.0);
        QCOMPARE(f.team("Red Bull").value("total_points").toDouble(), 183.0);
        QCOMPARE(f.team("Ferrari").value("total_points").toDouble(), 115.0);
        QCOMPARE(f.srv->championship().value("teams_title").toString(), QStringLiteral("Test-WM · Teams"));
        Fix g(wmDatei({{"title", "Fahrerwertung"}}));
        QCOMPARE(g.srv->championship().value("teams_title").toString(), QStringLiteral("Teamwertung"));
        Fix h(wmDatei({{"teams_title", "Konstrukteure"}}));
        QCOMPARE(h.srv->championship().value("teams_title").toString(), QStringLiteral("Konstrukteure"));
    }

    void teamBasisAusDatei()
    {
        // Gross/klein egal; ein Team ohne aktuelle Fahrer taucht trotzdem auf
        Fix f(wmDatei({{"teams", QJsonArray{QJsonObject{{"name", "red bull"}, {"base_points", 300}},
                                            QJsonObject{{"name", "Williams"}, {"base_points", 7}}}}}));
        QCOMPARE(f.team("red bull").value("base_points").toDouble(), 300.0);
        QCOMPARE(f.team("red bull").value("total_points").toDouble(), 333.0);
        QCOMPARE(f.team("Williams").value("total_points").toDouble(), 7.0);
        QCOMPARE(f.srv->championship().value("teams").toArray().size(), 3);
    }

    void eigenePunkte()
    {
        Fix f(wmDatei({{"points_system", QJsonObject{{"1", 20}, {"2", 15}, {"3", 12}}}}));
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 20.0);
        QCOMPARE(f.fahrer("Max").value("live_points").toDouble(), 15.0);
        QCOMPARE(f.fahrer("Lando").value("live_points").toDouble(), 12.0);
        QCOMPARE(f.srv->championship().value("custom_points").toBool(), true);
    }

    void kurzeListeAlsArray()
    {
        Fix f(wmDatei({{"points_system", QJsonArray{10, 8}}}));
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 10.0);
        QCOMPARE(f.fahrer("Max").value("live_points").toDouble(), 8.0);
        QCOMPARE(f.fahrer("Lando").value("live_points").toDouble(), 0.0);   // P3 steht nicht drin
    }

    void ungueltigIstStandard()
    {
        Fix f(wmDatei({{"points_system", QJsonObject{{"1", "abc"}, {"x", 5}, {"2", -3}, {"99", 4}}}}));
        QCOMPARE(f.srv->championship().value("custom_points").toBool(), false);
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 25.0);
    }

    void listeSpeichern()
    {
        Fix f(wmDatei());
        HttpResponse r = f.srv->handle(post("/api/champfile/points",
                                            {{"file", "championship.json"}, {"points_system", QJsonObject{{"1", 20}, {"2", 15}}}}));
        QCOMPARE(r.status, 200);
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 20.0);
        // Die uebrigen Schluessel der Datei bleiben
        QFile datei(f.dir.filePath("championship.json"));
        QVERIFY(datei.open(QIODevice::ReadOnly));
        const QJsonObject gespeichert = QJsonDocument::fromJson(datei.readAll()).object();
        datei.close();
        QCOMPARE(gespeichert.value("_comment").toString(), QStringLiteral("bleibt erhalten"));
        QCOMPARE(gespeichert.value("drivers").toArray().size(), 3);
        QCOMPARE(gespeichert.value("points_system").toObject().value("2").toInt(), 15);

        // Zurueck auf Standard
        r = f.srv->handle(post("/api/champfile/points", {{"file", "championship.json"}, {"points_system", QJsonValue::Null}}));
        QCOMPARE(r.status, 200);
        QCOMPARE(f.fahrer("Charles").value("live_points").toDouble(), 25.0);

        // Abgelehnt: zu viele Punkte, Kommazahl, fremde Datei, Pfad
        QCOMPARE(f.srv->handle(post("/api/champfile/points", {{"file", "championship.json"}, {"points_system", QJsonObject{{"1", 101}}}})).status, 400);
        QCOMPARE(f.srv->handle(post("/api/champfile/points", {{"file", "championship.json"}, {"points_system", QJsonObject{{"1", 2.5}}}})).status, 400);
        QCOMPARE(f.srv->handle(post("/api/champfile/points", {{"file", "gibtsnicht.json"}, {"points_system", QJsonArray{1}}})).status, 400);
        QCOMPARE(f.srv->handle(post("/api/champfile/points", {{"file", "../championship.json"}, {"points_system", QJsonArray{1}}})).status, 200);   // basename -> eigene Datei
        QCOMPARE(f.srv->handle(post("/api/champfile/points", {{"file", "access.json"}, {"points_system", QJsonArray{1}}})).status, 400);
    }
};

QTEST_GUILESS_MAIN(TstChamp)
#include "tst_champ.moc"
