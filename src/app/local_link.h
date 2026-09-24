// Das HUD am In-Process-Server - ohne Umweg ueber HTTP.
//
//   LocalFeed        Live-Zustand im Takt des HUD, direkt aus KersServer::livePayload()
//   LocalDataSource  Nebendaten (/api/track, /api/championship ...) und der Layout-
//                    Rueckweg (POST /api/settings) - ueber dieselben Routen wie
//                    die Web-Seiten, nur ohne Socket
//
// Dieselben Regeln wie beim SSE-Strom: gesendet wird, wenn neue UDP-Daten da sind,
// sonst einmal pro Sekunde. Ist der Server gestoppt, meldet der Feed "kein Link"
// und die Datenquelle antwortet wie ein nicht erreichbarer Server.
#pragma once

#include "data_source.h"
#include "feed.h"

#include <QPointer>
#include <QTimer>

namespace kers::server {
class KersServer;
}

namespace kers::app {

class LocalFeed : public hud::Feed {
    Q_OBJECT
public:
    LocalFeed(server::KersServer* server, int hz, QObject* parent = nullptr);
    void start() override;
    void stop() override;
    void setHz(int hz) override;

private:
    void tick();
    void setLinked(bool linked);

    QPointer<server::KersServer> m_server;
    QTimer m_timer;
    double m_lastSent = 0.0;
    bool m_linked = false;
    bool m_first = true;
};

class LocalDataSource : public hud::DataSource {
    Q_OBJECT
public:
    explicit LocalDataSource(server::KersServer* server, QObject* parent = nullptr);
    void get(const QString& path, JsonCb cb) override;
    void post(const QString& path, const QJsonObject& body, PostCb cb) override;
    QString baseUrl() const override;

private:
    QPointer<server::KersServer> m_server;
};

}  // namespace kers::app
