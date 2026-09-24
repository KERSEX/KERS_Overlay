#include "server_control.h"

#include "kers_server.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace kers::app {

RemoteStatus::RemoteStatus(const QString& baseUrl, QObject* parent)
    : ServerControl(parent)
    , m_source(baseUrl)
{
    m_timer.setInterval(INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &RemoteStatus::refresh);
}

void RemoteStatus::startPolling()
{
    refresh();
    m_timer.start();
}

void RemoteStatus::stopPolling()
{
    m_timer.stop();
}

void RemoteStatus::refresh()
{
    // Nur eine Abfrage auf einmal - bei einem haengenden Server stapeln sie sich sonst.
    if (m_pending)
        return;
    m_pending = true;
    // /api/status ist absichtlich der EINZIGE Endpunkt, den das Schaltbrett
    // anfasst - /api/settings und /api/regie gehoeren den Web-Seiten.
    QNetworkRequest req(QUrl(m_source.baseUrl() + QStringLiteral("/api/status")));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(1500);
    auto* nam = m_source.findChild<QNetworkAccessManager*>();
    Q_UNUSED(nam);
    // Eigene kleine Abfrage statt DataSource::get: die dedupliziert pro Pfad und
    // meldet Fehler nicht - hier ist "Server weg" aber genau die Information.
    static QNetworkAccessManager* mgr = nullptr;
    if (!mgr)
        mgr = new QNetworkAccessManager(this);
    QNetworkReply* reply = mgr->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_pending = false;
        bool ok = false, udp = false;
        int drivers = 0;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                ok = true;
                udp = doc.object().value(QLatin1String("connected")).toBool();
                drivers = doc.object().value(QLatin1String("driver_count")).toInt();
            }
        }
        if (m_force || ok != m_serverOk || udp != m_udpOk || drivers != m_drivers) {
            m_force = false;
            m_serverOk = ok;
            m_udpOk = udp;
            m_drivers = drivers;
            emit statusChanged(ok, udp, drivers);
        }
    });
}

// ── LocalServer ──────────────────────────────────────────────────────────────
LocalServer::LocalServer(server::KersServer& server, quint16 httpPort, quint16 udpPort, const QHostAddress& bind,
                         QObject* parent)
    : ServerControl(parent)
    , m_server(server)
    , m_httpPort(httpPort)
    , m_udpPort(udpPort)
    , m_bind(bind)
{
    m_timer.setInterval(INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &LocalServer::refresh);
    connect(&m_server, &server::KersServer::runningChanged, this, &LocalServer::refresh);
    m_timer.start();
}

bool LocalServer::running() const
{
    return m_server.isRunning();
}

void LocalServer::start()
{
    // Laeuft er schon (Autostart), trotzdem den Stand melden - sonst wartet das
    // Schaltbrett nach dem Klick vergeblich auf eine Aenderung.
    if (!m_server.isRunning() && !m_server.start(m_httpPort, m_udpPort, m_bind))
        emit errorOccurred(m_server.errorString());
    announce();
}

void LocalServer::stop()
{
    m_server.stop();
    announce();
}

QString LocalServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server.isRunning() ? m_server.httpPort() : m_httpPort);
}

void LocalServer::refresh()
{
    const bool ok = m_server.isRunning();
    const QJsonObject st = m_server.status();
    const bool udp = ok && st.value(QLatin1String("connected")).toBool();
    const int drivers = ok ? st.value(QLatin1String("driver_count")).toInt() : 0;
    if (m_force || ok != m_serverOk || udp != m_udpOk || drivers != m_drivers) {
        m_force = false;
        m_serverOk = ok;
        m_udpOk = udp;
        m_drivers = drivers;
        emit statusChanged(ok, udp, drivers);
    }
}

}  // namespace kers::app
