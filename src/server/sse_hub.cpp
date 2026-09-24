#include "sse_hub.h"

#include "http_server.h"

#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace kers::server {

namespace {
const char* const SLIM_KEYS[] = {"settings", "final_classification", "quali_results"};
}

struct SseHub::Client {
    QPointer<HttpConnection> conn;
    QTimer timer;
    double lastSent = 0.0;
    bool slim = false;
    QJsonObject lastSlim;
};

SseHub::SseHub(PayloadFn payload, TimeFn lastPacket, TimeFn clock, QObject* parent)
    : QObject(parent)
    , m_payload(std::move(payload))
    , m_lastPacket(std::move(lastPacket))
    , m_clock(std::move(clock))
{
}

SseHub::~SseHub()
{
    closeAll();
}

double SseHub::tickFor(const QString& hzArg)
{
    bool ok = false;
    double hz = hzArg.trimmed().toDouble(&ok);
    if (!ok || std::isnan(hz))
        hz = 0.0;
    return hz > 0 ? 1.0 / std::max(1.0, std::min(60.0, hz)) : DEFAULT_TICK;
}

void SseHub::add(HttpConnection* conn, double tickSeconds, bool slim)
{
    auto c = std::make_unique<Client>();
    Client* raw = c.get();
    raw->conn = conn;
    raw->slim = slim;
    raw->timer.setTimerType(Qt::PreciseTimer);
    raw->timer.setInterval(std::max(1, static_cast<int>(std::lround(tickSeconds * 1000.0))));
    connect(&raw->timer, &QTimer::timeout, this, [this, raw] { tick(raw); });
    connect(conn, &HttpConnection::closed, this, [this, raw] { remove(raw); });
    raw->timer.start();
    m_clients.push_back(std::move(c));
}

void SseHub::remove(Client* c)
{
    auto it = std::find_if(m_clients.begin(), m_clients.end(), [c](const auto& p) { return p.get() == c; });
    if (it == m_clients.end())
        return;
    (*it)->timer.stop();
    // Nicht mitten im eigenen Timer-Aufruf zerstoeren
    std::unique_ptr<Client> weg = std::move(*it);
    m_clients.erase(it);
    QTimer::singleShot(0, this, [p = weg.release()] { delete p; });
}

void SseHub::closeAll()
{
    auto alle = std::move(m_clients);
    m_clients.clear();
    for (auto& c : alle) {
        c->timer.stop();
        if (c->conn) {
            QObject::disconnect(c->conn, nullptr, this, nullptr);
            c->conn->close();
        }
    }
}

void SseHub::tick(Client* c)
{
    if (!c->conn) {
        remove(c);
        return;
    }
    const double now = m_clock();
    const bool fresh = m_lastPacket() > c->lastSent;
    if (!fresh && now - c->lastSent < 1.0)
        return;
    // Client liest nicht mehr mit -> diesen Takt auslassen, statt zu puffern
    if (c->conn->pendingBytes() > MAX_PENDING)
        return;
    QJsonObject payload = m_payload();
    c->lastSent = now;
    if (c->slim) {
        for (const char* k : SLIM_KEYS) {
            const QString key = QLatin1String(k);
            if (c->lastSlim.contains(key) && payload.value(key) == c->lastSlim.value(key))
                payload.remove(key);                      // unveraendert -> weglassen
            else
                c->lastSlim[key] = payload.value(key);
        }
    }
    QByteArray out = "data: ";
    out += QJsonDocument(payload).toJson(QJsonDocument::Compact);
    out += "\n\n";
    c->conn->write(out);
}

}  // namespace kers::server
