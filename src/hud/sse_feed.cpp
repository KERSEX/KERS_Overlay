#include "sse_feed.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

namespace kers::hud {

namespace {
// ⚠ Muss zu SLIM_KEYS in main.py passen.
const char* const kSlimKeys[] = {"settings", "final_classification", "quali_results"};
}

SseFeed::SseFeed(const QString& baseUrl, int hz, QObject* parent)
    : Feed(parent)
    , m_hz(hz)
    , m_nam(this)
{
    setBaseUrl(baseUrl);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &SseFeed::connectStream);
}

SseFeed::~SseFeed()
{
    stop();
}

void SseFeed::start()
{
    if (m_running)
        return;
    m_running = true;
    m_tries = 0;
    connectStream();
}

void SseFeed::stop()
{
    m_running = false;
    m_retry.stop();
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_buffer.clear();
}

void SseFeed::setBaseUrl(const QString& baseUrl)
{
    QString neu = baseUrl;
    while (neu.endsWith(QLatin1Char('/')))
        neu.chop(1);
    if (neu == m_base)
        return;
    m_base = neu;
    if (m_running) {
        stop();
        start();
    }
}

void SseFeed::setHz(int hz)
{
    if (hz == m_hz)
        return;
    m_hz = hz;
    if (m_running) {
        stop();
        start();
    }
}

void SseFeed::setLinked(bool linked)
{
    if (linked == m_linked)
        return;
    m_linked = linked;
    emit linkChanged(linked);
}

QJsonObject SseFeed::mergeSlim(QJsonObject data)
{
    for (const char* k : kSlimKeys) {
        const QLatin1String key(k);
        if (data.contains(key))
            m_slimCache[key] = data.value(key);
        else if (m_slimCache.contains(key))
            data[key] = m_slimCache.value(key);
    }
    return data;
}

void SseFeed::connectStream()
{
    if (!m_running || m_reply)
        return;
    m_got = false;
    m_buffer.clear();
    QNetworkRequest req(QUrl(QStringLiteral("%1/api/stream?hz=%2&slim=1").arg(m_base).arg(m_hz)));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    // Der Timeout muss ueber dem Heartbeat des Servers liegen (der schickt
    // mindestens 1x/s), sonst reisst eine ruhige Verbindung staendig ab.
    req.setTransferTimeout(10000);
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &SseFeed::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &SseFeed::onFinished);
}

void SseFeed::handleLine(const QByteArray& line)
{
    if (!line.startsWith("data: "))
        return;                       // Leerzeile = Ereignisende, Kommentare ignorieren
    const QJsonDocument doc = QJsonDocument::fromJson(line.mid(6));
    if (!doc.isObject())
        return;                       // halbes Paket - der naechste Payload heilt es
    m_got = true;
    setLinked(true);
    emit payload(mergeSlim(doc.object()));
}

void SseFeed::onReadyRead()
{
    if (!m_reply)
        return;
    m_buffer += m_reply->readAll();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(line);
    }
}

void SseFeed::onFinished()
{
    if (!m_reply)
        return;
    QNetworkReply* r = m_reply;
    m_reply = nullptr;
    r->deleteLater();
    if (!m_running)
        return;
    setLinked(false);
    // Ein Strom, der sofort wieder abreisst, ist ein anderer Fall als einer, der
    // lange lief: nur beim ERSTEN Scheitern in Folge hochzaehlen.
    m_tries = m_got ? 0 : m_tries + 1;
    if (m_tries > RECONNECT_FAST_TRIES) {
        // SSE kommt offenbar nicht durch (Proxy, Virenscanner) -> pollen.
        pollOnce();
        m_retry.start(m_tries % 10 == 0 ? RECONNECT_SLOW_MS : POLL_INTERVAL_MS);
    } else {
        m_retry.start(RECONNECT_FAST_MS);
    }
}

void SseFeed::pollOnce()
{
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/api/live")));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(2000);
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!m_running)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            setLinked(false);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
            return;
        setLinked(true);
        emit payload(mergeSlim(doc.object()));
    });
}

}  // namespace kers::hud
