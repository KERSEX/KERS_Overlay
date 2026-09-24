// Die Schnittstelle zwischen Schaltbrett und Server.
//
// Das Schaltbrett will nur wissen: laeuft der Server, kommen UDP-Daten, und kann
// es ihn starten/stoppen. Dahinter stecken zwei Fassungen:
//   RemoteStatus   fragt einen fremden Server per HTTP (/api/status) im Sekundentakt -
//                  Remote-Betrieb mit --url, und in M1 der noch laufende Python-Server.
//                  Starten/Stoppen gibt es dort nicht.
//   LocalServer    der In-Process-Server (ab M3): Start/Stop direkt, Status ohne Netz.
#pragma once

#include "data_source.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>

namespace kers::server {
class KersServer;
}

namespace kers::app {

class ServerControl : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool canStart() const = 0;
    virtual bool running() const = 0;
    virtual void start() {}
    virtual void stop() {}
    virtual QString baseUrl() const = 0;
    /// Status sofort neu abfragen (nach Start/Stop). Meldet nur Aenderungen.
    virtual void refresh() {}
    /// Den aktuellen Stand auf jeden Fall melden - auch wenn sich nichts
    /// geaendert hat. Fuer alle, die sich spaeter anmelden (das Schaltbrett
    /// entsteht erst NACH dem Autostart des Servers).
    void announce()
    {
        m_force = true;
        refresh();
    }
signals:
    void statusChanged(bool serverOk, bool udpOk, int drivers);
    /// Start ging schief (Port belegt ...) - fuer eine sichtbare Meldung.
    void errorOccurred(const QString& message);

protected:
    /// Beim naechsten Ergebnis melden, auch ohne Aenderung.
    bool m_force = false;
};

/// Fremder Server per HTTP - Portierung von hud/api_client.py.
class RemoteStatus : public ServerControl {
    Q_OBJECT
public:
    static constexpr int INTERVAL_MS = 1000;

    explicit RemoteStatus(const QString& baseUrl, QObject* parent = nullptr);
    bool canStart() const override { return false; }
    bool running() const override { return m_serverOk; }
    QString baseUrl() const override { return m_source.baseUrl(); }
    void setBaseUrl(const QString& url) { m_source.setBaseUrl(url); }
    void refresh() override;
    void startPolling();
    void stopPolling();

private:
    hud::HttpDataSource m_source;
    QTimer m_timer;
    bool m_serverOk = false;
    bool m_udpOk = false;
    int m_drivers = 0;
    bool m_pending = false;
};

/// Der In-Process-Server (M3): Start/Stop direkt, Status ohne Netz.
class LocalServer : public ServerControl {
    Q_OBJECT
public:
    static constexpr int INTERVAL_MS = 1000;

    /// bind: Vorgabe alle Adressen (Handy, zweiter PC); Tests nehmen 127.0.0.1.
    LocalServer(server::KersServer& server, quint16 httpPort, quint16 udpPort,
                const QHostAddress& bind = QHostAddress::AnyIPv4, QObject* parent = nullptr);
    bool canStart() const override { return true; }
    bool running() const override;
    void start() override;
    void stop() override;
    QString baseUrl() const override;
    void refresh() override;

private:
    server::KersServer& m_server;
    quint16 m_httpPort;
    quint16 m_udpPort;
    QHostAddress m_bind;
    QTimer m_timer;
    bool m_serverOk = false;
    bool m_udpOk = false;
    int m_drivers = -1;
};

}  // namespace kers::app
