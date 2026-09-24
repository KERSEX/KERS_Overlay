// SSE-Client gegen /api/stream - Portierung von hud/feed.py, aber ohne Thread:
// ein streamender QNetworkReply liefert die Zeilen per readyRead in den Eventloop.
//
// Zwei Feinheiten, beide vom Server vorgesehen:
//   ?hz=N     begrenzt den Sendetakt
//   ?slim=1   schickt settings/final_classification/quali_results nur bei
//             Aenderung - der Client behaelt den letzten Stand (mergeSlim)
//
// Faellt der Strom aus, wird nach acht schnellen Versuchen auf Polling von
// /api/live zurueckgeschaltet (wie startPolling() in core.js).
#pragma once

#include "feed.h"

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>

namespace kers::hud {

class QNetworkReplyFwd;

class SseFeed : public Feed {
    Q_OBJECT
public:
    static constexpr int RECONNECT_FAST_MS = 2000;   // erste Versuche: der Server kommt meist gleich
    static constexpr int RECONNECT_SLOW_MS = 15000;  // danach: er ist offenbar wirklich aus
    static constexpr int RECONNECT_FAST_TRIES = 8;
    static constexpr int POLL_INTERVAL_MS = 200;     // Rueckfallebene

    SseFeed(const QString& baseUrl, int hz = 30, QObject* parent = nullptr);
    ~SseFeed() override;

    void start() override;
    void stop() override;
    void setBaseUrl(const QString& baseUrl) override;
    void setHz(int hz) override;

private:
    void connectStream();
    void onReadyRead();
    void onFinished();
    void pollOnce();
    void setLinked(bool linked);
    QJsonObject mergeSlim(QJsonObject data);
    void handleLine(const QByteArray& line);

    QString m_base;
    int m_hz;
    QNetworkAccessManager m_nam;
    class QNetworkReply* m_reply = nullptr;
    QByteArray m_buffer;
    QTimer m_retry;
    bool m_running = false;
    bool m_linked = false;
    bool m_got = false;
    int m_tries = 0;
    QJsonObject m_slimCache;
};

}  // namespace kers::hud
