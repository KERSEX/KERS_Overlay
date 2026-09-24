// Ein kleiner HTTP/1.1-Server auf QTcpServer - Ersatz fuer Flask.
//
// Gebraucht wird nur ein schmaler Ausschnitt: GET/POST/HEAD mit Content-Length,
// Keep-Alive, und Server-Sent Events als offener Datenstrom. Kein Chunked-Upload,
// kein TLS, keine Pipelines ueber mehrere offene Anfragen hinweg.
//
// Warum nicht QHttpServer: SSE-Clients muessen sauber abgemeldet werden, sobald
// OBS oder das Handy die Verbindung schliesst - QHttpServer bietet dafuer nur
// Polling. Und das Modul muesste zusaetzlich statisch gebaut werden.
//
// Alles laeuft im Haupt-Thread, wie der Rest des Servers.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>

#include <functional>

class QTcpSocket;

namespace kers::server {

class HttpConnection;

struct HttpRequest {
    QByteArray method;                       // "GET", "POST", "HEAD" ... (HEAD kommt als GET beim Handler an)
    QString path;                            // dekodiert, ohne Query
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers;   // Namen klein geschrieben
    QByteArray body;
    QHostAddress peer;

    QByteArray header(const QByteArray& lowerName) const { return headers.value(lowerName); }
    /// request.get_json(silent=True) or {} - ein Objekt, sonst leer.
    QJsonObject jsonObject() const;
    /// request.args.get(name, "")
    QString arg(const QString& name) const { return query.queryItemValue(name, QUrl::FullyDecoded); }
};

struct HttpResponse {
    int status = 200;
    QByteArray contentType = "text/plain; charset=utf-8";
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> headers;
    /// Gesetzt = Event-Stream: nach dem Kopf ruft der Server das auf und gibt die
    /// Verbindung ab. Auf ihr laufen danach keine weiteren Anfragen.
    std::function<void(HttpConnection*)> stream;

    static HttpResponse json(const QJsonObject& o, int status = 200);
    static HttpResponse html(const QByteArray& body, int status = 200);
    static HttpResponse error(int status, const QByteArray& message = {});
    static HttpResponse methodNotAllowed(const QByteArray& allow);
    static HttpResponse eventStream(std::function<void(HttpConnection*)> onOpen);
    static QByteArray reason(int status);

    void addHeader(const QByteArray& name, const QByteArray& value) { headers.append(qMakePair(name, value)); }
};

class HttpServer;

/// Eine TCP-Verbindung. Liest Anfragen nacheinander, beantwortet sie und bleibt
/// bei Keep-Alive offen - oder wird zum Event-Stream.
class HttpConnection : public QObject {
    Q_OBJECT
    friend class HttpServer;
public:
    static constexpr int MAX_HEADER = 16 * 1024;
    static constexpr int MAX_BODY = 2 * 1024 * 1024;
    static constexpr int IDLE_MS = 60000;

    HttpConnection(QTcpSocket* socket, HttpServer* server);
    ~HttpConnection() override;

    bool isStreaming() const { return m_streaming; }
    /// Im Stream-Modus: Rohdaten senden.
    void write(const QByteArray& data);
    /// Noch nicht an das Netz uebergebene Bytes - waechst, wenn der Client nicht mehr liest.
    qint64 pendingBytes() const;
    QHostAddress peer() const;
    void close();

signals:
    void closed();

private:
    void onReadyRead();
    bool processOne();
    void respond(const HttpRequest& req, HttpResponse res, bool keepAlive, bool head);
    void fail(int status);
    void finish();

    QTcpSocket* m_socket;
    HttpServer* m_server;
    QByteArray m_buf;
    QTimer m_idle;
    bool m_streaming = false;
    bool m_closing = false;
    bool m_sentContinue = false;
    bool m_finished = false;
};

class HttpServer : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit HttpServer(Handler handler, QObject* parent = nullptr);
    ~HttpServer() override;

    bool listen(const QHostAddress& address, quint16 port);
    /// Nimmt keine neuen Verbindungen mehr an und schliesst alle offenen.
    void close();
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }
    QString errorString() const { return m_server.errorString(); }
    int connectionCount() const { return m_conns.size(); }

    HttpResponse dispatch(const HttpRequest& req) const { return m_handler(req); }

private:
    friend class HttpConnection;
    void onNewConnection();
    void forget(HttpConnection* c) { m_conns.remove(c); }

    QTcpServer m_server;
    Handler m_handler;
    QSet<HttpConnection*> m_conns;
};

/// Mimetype zu einem Dateinamen (wie Flask/mimetypes, fuer die Dateien in static/).
QByteArray mimeFor(const QString& fileName);

}  // namespace kers::server
