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

    void add(HttpConnection* conn, double tickSeconds, bool slim);
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

}  // namespace kers::server
