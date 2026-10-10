// /api/stream - Server-Sent Events fuer Overlay, Regie, Handy und OBS.
//
// Wie api_stream() in main.py: jeder Client hat seinen eigenen Takt (Standard
// 80 ms, mit ?hz=N zwischen 1 und 60), gesendet wird, sobald neue UDP-Daten da
// sind - sonst einmal pro Sekunde als Lebenszeichen. Mit ?slim=1 gehen die fast
// statischen Teile (settings, final_classification, quali_results) nur bei einer
// Aenderung mit.
//
// Neu gegenueber Flask: liest ein Client nicht mehr (OBS-Quelle versteckt, Handy
// im Standby), wird fuer ihn uebersprungen, statt den Speicher vollaufen zu lassen.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace kers::server {

class HttpConnection;

class SseHub : public QObject {
    Q_OBJECT
public:
    static constexpr double DEFAULT_TICK = 0.08;
    static constexpr qint64 MAX_PENDING = 512 * 1024;

    using PayloadFn = std::function<QJsonObject()>;
    using TimeFn = std::function<double()>;

    /// payload: baut den Live-Zustand; lastPacket: Zeit des letzten UDP-Pakets; clock: Wanduhr (s)
    SseHub(PayloadFn payload, TimeFn lastPacket, TimeFn clock, QObject* parent = nullptr);
    ~SseHub() override;

    /// ?hz= auswerten wie main.py: float, 1..60, sonst 12,5 Hz.
    static double tickFor(const QString& hzArg);

    /// deviceId: freigegebenes fremdes Geraet (leer = dieser PC oder Schutz aus);
    /// local: kommt von diesem PC.
    void add(HttpConnection* conn, double tickSeconds, bool slim, const QString& deviceId = QString(),
             bool local = true);
    /// Stroeme kappen, wenn ein Geraet/eine IP gesperrt wird. Beide leer = alle
    /// fremden Stroeme ohne Geraet (Schutz wurde gerade eingeschaltet).
    void closeMatching(const QString& ip, const QString& deviceId);
    int clientCount() const { return static_cast<int>(m_clients.size()); }
    void closeAll();

private:
    struct Client;
    void tick(Client* c);
    void remove(Client* c);

    PayloadFn m_payload;
    TimeFn m_lastPacket;
    TimeFn m_clock;
    std::vector<std::unique_ptr<Client>> m_clients;
};

/// Ein schlichter Ereignis-Kanal: jede Nachricht geht sofort an alle (z. B. der
/// OBS-Zustand an die Regie, /api/obs/stream). Eigener Kanal statt /api/stream,
/// damit fremde Ereignisse nie im Overlay-Takt landen - das Python-HUD und aeltere
/// Seiten kennen nur "data:" mit dem Live-Zustand.
class EventChannel : public QObject {
    Q_OBJECT
public:
    explicit EventChannel(QObject* parent = nullptr);
    ~EventChannel() override;
    /// first: gleich nach dem Oeffnen senden (der aktuelle Stand)
    void add(HttpConnection* conn, const QByteArray& first, const QString& deviceId = QString(), bool local = true);
    void send(const QByteArray& json);
    void closeMatching(const QString& ip, const QString& deviceId);
    void closeAll();
    int count() const { return static_cast<int>(m_clients.size()); }

private:
    struct Client {
        QPointer<HttpConnection> conn;
        QString deviceId;
        QString ip;
        bool local = true;
    };
    std::vector<Client> m_clients;
    QTimer m_ping;
};

}  // namespace kers::server
