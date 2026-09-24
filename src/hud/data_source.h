// Woher die Bausteine ihre Nebendaten holen - Gegenstueck zu _Fetcher in extras.py
// und zum QNetworkAccessManager in bridge.py.
//
// Zwei Fassungen hinter einer Schnittstelle:
//   HttpDataSource   fragt einen Server per HTTP (Remote-Betrieb mit --url,
//                    und in M1 der noch laufende Python-Server)
//   LocalDataSource  ruft den In-Process-Server direkt (ab M3)
//
// Laufende GET-Abfragen werden je Pfad dedupliziert - bei langsamem Server
// stapeln sich sonst Requests.
#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

namespace kers::hud {

class DataSource : public QObject {
    Q_OBJECT
public:
    using JsonCb = std::function<void(const QJsonObject&)>;
    using PostCb = std::function<void(bool ok, const QJsonObject& antwort, const QString& fehler)>;

    using QObject::QObject;
    virtual void get(const QString& path, JsonCb cb) = 0;
    virtual void post(const QString& path, const QJsonObject& body, PostCb cb) = 0;
    virtual void setBaseUrl(const QString&) {}
    virtual QString baseUrl() const { return QString(); }
};

class HttpDataSource : public DataSource {
    Q_OBJECT
public:
    explicit HttpDataSource(const QString& baseUrl, QObject* parent = nullptr);
    void get(const QString& path, JsonCb cb) override;
    void post(const QString& path, const QJsonObject& body, PostCb cb) override;
    void setBaseUrl(const QString& url) override;
    QString baseUrl() const override { return m_base; }

private:
    QString m_base;
    QNetworkAccessManager m_nam;
    QSet<QString> m_busy;
};

}  // namespace kers::hud
