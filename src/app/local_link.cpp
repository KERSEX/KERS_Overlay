#include "local_link.h"

#include "kers_server.h"

#include <QJsonDocument>
#include <QUrl>

#include <algorithm>

namespace kers::app {

namespace {

server::HttpRequest anfrage(const QByteArray& methode, const QString& pfad, const QByteArray& body = {})
{
    server::HttpRequest r;
    r.method = methode;
    const QUrl u(QStringLiteral("http://lokal") + pfad);
    r.path = u.path();
    r.query = QUrlQuery(u.query());
    r.body = body;
    r.peer = QHostAddress::LocalHost;
    if (!body.isEmpty())
        r.headers.insert("content-type", "application/json");
    return r;
}

}  // namespace

// ── LocalFeed ────────────────────────────────────────────────────────────────
LocalFeed::LocalFeed(server::KersServer* server, int hz, QObject* parent)
    : Feed(parent)
    , m_server(server)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    setHz(hz);
    connect(&m_timer, &QTimer::timeout, this, &LocalFeed::tick);
}

void LocalFeed::setHz(int hz)
{
    // Wie ?hz= beim SSE-Strom: 1..60, mehr sendet das Spiel ohnehin nicht
    const int h = std::clamp(hz > 0 ? hz : 30, 1, 60);
    m_timer.setInterval(std::max(1, 1000 / h));
}

void LocalFeed::start()
{
    m_first = true;
    m_lastSent = 0.0;
    m_timer.start();
    tick();
}

void LocalFeed::stop()
{
    m_timer.stop();
    setLinked(false);
}

void LocalFeed::setLinked(bool linked)
{
    if (linked == m_linked)
        return;
    m_linked = linked;
    emit linkChanged(linked);
}

void LocalFeed::tick()
{
    if (!m_server || !m_server->isRunning()) {
        setLinked(false);
        m_first = true;
        return;
    }
    setLinked(true);
    const double now = m_server->now();
    const bool fresh = m_server->state().lastPacketTime() > m_lastSent;
    if (!m_first && !fresh && now - m_lastSent < 1.0)
        return;
    m_first = false;
    m_lastSent = now;
    emit payload(m_server->livePayload());
}

// ── LocalDataSource ──────────────────────────────────────────────────────────
LocalDataSource::LocalDataSource(server::KersServer* server, QObject* parent)
    : DataSource(parent)
    , m_server(server)
{
}

QString LocalDataSource::baseUrl() const
{
    return m_server ? QStringLiteral("http://127.0.0.1:%1").arg(m_server->httpPort()) : QString();
}

void LocalDataSource::get(const QString& path, JsonCb cb)
{
    // Asynchron wie beim Netz: der Aufrufer darf sich darauf verlassen, dass die
    // Antwort NICHT noch innerhalb seines eigenen Aufrufs kommt.
    QTimer::singleShot(0, this, [this, path, cb] {
        if (!m_server || !m_server->isRunning() || !cb)
            return;                               // wie "Server weg": stiller Neuversuch beim naechsten Takt
        const server::HttpResponse r = m_server->handle(anfrage("GET", path));
        if (r.status != 200)
            return;
        const QJsonDocument doc = QJsonDocument::fromJson(r.body);
        if (doc.isObject())
            cb(doc.object());
    });
}

void LocalDataSource::post(const QString& path, const QJsonObject& body, PostCb cb)
{
    const QByteArray raw = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QTimer::singleShot(0, this, [this, path, raw, cb] {
        if (!cb)
            return;
        if (!m_server || !m_server->isRunning()) {
            cb(false, {}, QStringLiteral("Server ist aus"));
            return;
        }
        const server::HttpResponse r = m_server->handle(anfrage("POST", path, raw));
        const QJsonDocument doc = QJsonDocument::fromJson(r.body);
        if (r.status != 200) {
            cb(false, doc.object(), QStringLiteral("HTTP %1").arg(r.status));
            return;
        }
        if (!doc.isObject()) {
            cb(false, {}, QStringLiteral("Antwort ist kein JSON-Objekt"));
            return;
        }
        cb(true, doc.object(), QString());
    });
}

}  // namespace kers::app
