#include "http_server.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QDebug>

namespace kers::server {

// ── Anfrage / Antwort ────────────────────────────────────────────────────────
QJsonObject HttpRequest::jsonObject() const
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

HttpResponse HttpResponse::json(const QJsonObject& o, int status)
{
    HttpResponse r;
    r.status = status;
    r.contentType = "application/json";
    r.body = QJsonDocument(o).toJson(QJsonDocument::Compact);
    r.body.append('\n');
    return r;
}

HttpResponse HttpResponse::html(const QByteArray& body, int status)
{
    HttpResponse r;
    r.status = status;
    r.contentType = "text/html; charset=utf-8";
    r.body = body;
    return r;
}

HttpResponse HttpResponse::error(int status, const QByteArray& message)
{
    const QByteArray title = QByteArray::number(status) + ' ' + reason(status);
    QByteArray body = "<!doctype html>\n<html lang=en>\n<title>" + title + "</title>\n<h1>" + reason(status) + "</h1>\n";
    if (!message.isEmpty())
        body += "<p>" + QString::fromUtf8(message).toHtmlEscaped().toUtf8() + "</p>\n";
    return html(body, status);
}

HttpResponse HttpResponse::methodNotAllowed(const QByteArray& allow)
{
    HttpResponse r = error(405, "The method is not allowed for the requested URL.");
    r.addHeader("Allow", allow);
    return r;
}

HttpResponse HttpResponse::eventStream(std::function<void(HttpConnection*)> onOpen)
{
    HttpResponse r;
    r.contentType = "text/event-stream; charset=utf-8";
    r.addHeader("X-Accel-Buffering", "no");
    r.stream = std::move(onOpen);
    return r;
}

QByteArray HttpResponse::reason(int status)
{
    switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 204: return "No Content";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 505: return "HTTP Version Not Supported";
    default: return "Unknown";
    }
}

QByteArray mimeFor(const QString& fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) return "text/html; charset=utf-8";
    if (ext == QLatin1String("css")) return "text/css; charset=utf-8";
    if (ext == QLatin1String("js")) return "text/javascript; charset=utf-8";
    if (ext == QLatin1String("json")) return "application/json";
    if (ext == QLatin1String("txt")) return "text/plain; charset=utf-8";
    if (ext == QLatin1String("png")) return "image/png";
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return "image/jpeg";
    if (ext == QLatin1String("gif")) return "image/gif";
    if (ext == QLatin1String("webp")) return "image/webp";
    if (ext == QLatin1String("svg")) return "image/svg+xml";
    if (ext == QLatin1String("ico")) return "image/vnd.microsoft.icon";
    if (ext == QLatin1String("woff2")) return "font/woff2";
    if (ext == QLatin1String("woff")) return "font/woff";
    if (ext == QLatin1String("ttf")) return "font/ttf";
    return "application/octet-stream";
}

// ── Verbindung ───────────────────────────────────────────────────────────────
HttpConnection::HttpConnection(QTcpSocket* socket, HttpServer* server)
    : QObject(server)
    , m_socket(socket)
    , m_server(server)
{
    m_socket->setParent(this);
    m_idle.setSingleShot(true);
    m_idle.setInterval(IDLE_MS);
    connect(&m_idle, &QTimer::timeout, this, &HttpConnection::close);
    connect(m_socket, &QTcpSocket::readyRead, this, &HttpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &HttpConnection::finish);
    m_idle.start();
}

HttpConnection::~HttpConnection()
{
    if (m_server)
        m_server->forget(this);
}

QHostAddress HttpConnection::peer() const
{
    return m_socket->peerAddress();
}

qint64 HttpConnection::pendingBytes() const
{
    return m_socket->bytesToWrite();
}

void HttpConnection::write(const QByteArray& data)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(data);
}

void HttpConnection::close()
{
    if (m_closing)
        return;
    m_closing = true;
    m_idle.stop();
    // disconnectFromHost schickt noch Ausstehendes und meldet dann "disconnected",
    // darauf raeumt der Handler oben die Verbindung weg.
    m_socket->disconnectFromHost();
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        finish();
}

void HttpConnection::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_idle.stop();
    if (m_server)
        m_server->forget(this);
    emit closed();
    deleteLater();
}

void HttpConnection::onReadyRead()
{
    if (m_streaming || m_closing) {
        m_socket->readAll();                  // Event-Stream: der Client sendet nichts mehr, was zaehlt
        return;
    }
    m_buf.append(m_socket->readAll());
    m_idle.start();
    while (!m_streaming && !m_closing && processOne()) {
    }
}

void HttpConnection::fail(int status)
{
    HttpRequest dummy;
    respond(dummy, HttpResponse::error(status), false, false);
}

bool HttpConnection::processOne()
{
    const int headEnd = m_buf.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        if (m_buf.size() > MAX_HEADER)
            fail(431);
        return false;
    }
    if (headEnd > MAX_HEADER) {
        fail(431);
        return false;
    }

    const QList<QByteArray> lines = m_buf.left(headEnd).split('\n');
    const QList<QByteArray> reqLine = lines.value(0).trimmed().split(' ');
    if (reqLine.size() != 3 || !reqLine.at(2).startsWith("HTTP/1.")) {
        fail(400);
        return false;
    }
    HttpRequest req;
    req.method = reqLine.at(0).toUpper();
    const bool http10 = reqLine.at(2) == "HTTP/1.0";
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    if (req.header("transfer-encoding").toLower().contains("chunked")) {
        fail(501);
        return false;
    }
    bool okLen = true;
    const qint64 len = req.headers.contains("content-length") ? req.header("content-length").toLongLong(&okLen) : 0;
    if (!okLen || len < 0) {
        fail(400);
        return false;
    }
    if (len > MAX_BODY) {
        fail(413);
        return false;
    }
    const qint64 total = headEnd + 4 + len;
    if (m_buf.size() < total) {
        // curl & Co. warten bei groesseren Rumpfen auf ein "100 Continue"
        if (!m_sentContinue && req.header("expect").toLower() == "100-continue") {
            m_socket->write("HTTP/1.1 100 Continue\r\n\r\n");
            m_sentContinue = true;
        }
        return false;
    }
    req.body = m_buf.mid(headEnd + 4, len);
    m_buf.remove(0, total);
    m_sentContinue = false;

    // Ziel: "/pfad?query" oder absolut "http://host/pfad?query"
    QByteArray target = reqLine.at(1);
    if (!target.startsWith('/')) {
        const QUrl u(QString::fromUtf8(target));
        target = u.path(QUrl::FullyEncoded).toUtf8();
        if (u.hasQuery())
            target += '?' + u.query(QUrl::FullyEncoded).toUtf8();
        if (target.isEmpty())
            target = "/";
    }
    const int q = target.indexOf('?');
    const QByteArray rawPath = q >= 0 ? target.left(q) : target;
    req.path = QUrl::fromPercentEncoding(rawPath);
    if (q >= 0) {
        QByteArray qs = target.mid(q + 1);
        qs.replace('+', ' ');                 // Formular-Kodierung wie werkzeug
        req.query = QUrlQuery(QString::fromUtf8(qs));
    }
    req.peer = m_socket->peerAddress();

    const QByteArray conn = req.header("connection").toLower();
    const bool keepAlive = http10 ? conn.contains("keep-alive") : !conn.contains("close");
    const bool head = req.method == "HEAD";
    if (head)
        req.method = "GET";
    respond(req, m_server ? m_server->dispatch(req) : HttpResponse::error(500), keepAlive, head);
    return true;
}

void HttpConnection::respond(const HttpRequest& req, HttpResponse res, bool keepAlive, bool head)
{
    // Wie _no_cache in main.py: API und HTML-Seiten nie cachen (OBS und die
    // Android-WebView zeigten sonst nach einer Aenderung die alte Seite).
    if (req.path.startsWith(QLatin1String("/api/")) || res.contentType.startsWith("text/html")) {
        res.addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        res.addHeader("Pragma", "no-cache");
    }
    const bool stream = static_cast<bool>(res.stream) && !head;
    if (stream)
        keepAlive = true;
    if (res.closeConnection)
        keepAlive = false;

    QByteArray out;
    out.reserve(256 + (head ? 0 : res.body.size()));
    out += "HTTP/1.1 " + QByteArray::number(res.status) + ' ' + HttpResponse::reason(res.status) + "\r\n";
    out += "Server: KERS\r\n";
    out += "Content-Type: " + res.contentType + "\r\n";
    if (!stream)
        out += "Content-Length: " + QByteArray::number(res.body.size()) + "\r\n";
    out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    for (const auto& [name, value] : res.headers)
        out += name + ": " + value + "\r\n";
    out += "\r\n";
    if (!head && !stream)
        out += res.body;
    m_socket->write(out);

    if (stream) {
        m_streaming = true;
        m_idle.stop();
        m_buf.clear();
        res.stream(this);
        return;
    }
    if (!keepAlive)
        close();
}

// ── Server ───────────────────────────────────────────────────────────────────
HttpServer::HttpServer(Handler handler, QObject* parent)
    : QObject(parent)
    , m_handler(std::move(handler))
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

HttpServer::~HttpServer()
{
    close();
}

bool HttpServer::listen(const QHostAddress& address, quint16 port)
{
    return m_server.listen(address, port);
}

void HttpServer::close()
{
    m_server.close();
    const QSet<HttpConnection*> alle = m_conns;
    m_conns.clear();
    for (HttpConnection* c : alle) {
        c->m_server = nullptr;
        c->close();
    }
}

void HttpServer::onNewConnection()
{
    while (QTcpSocket* s = m_server.nextPendingConnection()) {
        auto* c = new HttpConnection(s, this);
        m_conns.insert(c);
    }
}

}  // namespace kers::server
