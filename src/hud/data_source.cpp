#include "data_source.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace kers::hud {

namespace {
QByteArray& schluessel()
{
    static QByteArray k;
    return k;
}
}  // namespace

void setAccessKey(const QByteArray& key)
{
    schluessel() = key.trimmed();
}

QByteArray accessKey()
{
    return schluessel();
}

void applyAccessKey(QNetworkRequest& req)
{
    if (!schluessel().isEmpty())
        req.setRawHeader("X-KERS-Key", schluessel());
}

HttpDataSource::HttpDataSource(const QString& baseUrl, QObject* parent)
    : DataSource(parent)
    , m_nam(this)
{
    setBaseUrl(baseUrl);
}

void HttpDataSource::setBaseUrl(const QString& url)
{
    m_base = url;
    while (m_base.endsWith(QLatin1Char('/')))
        m_base.chop(1);
}

void HttpDataSource::get(const QString& path, JsonCb cb)
{
    if (m_busy.contains(path))
        return;                       // noch eine Abfrage laeuft - nicht stapeln
    m_busy.insert(path);
    QNetworkRequest req(QUrl(m_base + path));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(5000);
    applyAccessKey(req);
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, cb]() {
        m_busy.remove(path);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;                   // Server weg -> stiller Neuversuch beim naechsten Takt
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject() && cb)
            cb(doc.object());
    });
}

void HttpDataSource::post(const QString& path, const QJsonObject& body, PostCb cb)
{
    QNetworkRequest req(QUrl(m_base + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(5000);
    applyAccessKey(req);
    QNetworkReply* reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        reply->deleteLater();
        if (!cb)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            cb(false, {}, reply->errorString());
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            cb(false, {}, QStringLiteral("Antwort ist kein JSON-Objekt"));
            return;
        }
        cb(true, doc.object(), QString());
    });
}

}  // namespace kers::hud
