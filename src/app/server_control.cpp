#include "server_control.h"

#include "kers_server.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QThread>
#include <QUrl>

#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace kers::app {

namespace {
QString pfadSchluessel(const QString& p)
{
    // Windows-Pfade sind gross/klein-unabhaengig; Trenner vereinheitlichen.
    return QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
}
}  // namespace

QStringList alteServerBeenden(const QStringList& pfade, int warteMs)
{
    QStringList weg;
#ifdef Q_OS_WIN
    QSet<QString> ziele, namen;
    for (const QString& p : pfade) {
        const QFileInfo fi(p);
        ziele.insert(pfadSchluessel(fi.absoluteFilePath()));
        namen.insert(fi.fileName().toLower());
    }
    if (ziele.isEmpty())
        return weg;

    struct Treffer {
        DWORD pid;
        DWORD eltern;
        HANDLE h;
    };
    QList<Treffer> treffer;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return weg;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        // Der Name ist nur der Vorfilter - entscheidend ist der volle Pfad.
        if (!namen.contains(QString::fromWCharArray(pe.szExeFile).toLower()))
            continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                               pe.th32ProcessID);
        if (!h)
            continue;
        wchar_t buf[1024];
        DWORD len = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(h, 0, buf, &len)
            && ziele.contains(pfadSchluessel(QString::fromWCharArray(buf, int(len)))))
            treffer.append({pe.th32ProcessID, pe.th32ParentProcessID, h});
        else
            CloseHandle(h);
    }
    CloseHandle(snap);

    QSet<DWORD> pids;
    for (const Treffer& t : treffer)
        pids.insert(t.pid);
    auto hatKind = [&](DWORD pid) {
        for (const Treffer& t : treffer)
            if (t.eltern == pid)
                return true;
        return false;
    };
    auto beenden = [&](const Treffer& t) {
        if (WaitForSingleObject(t.h, 0) != WAIT_OBJECT_0 && TerminateProcess(t.h, 1))
            WaitForSingleObject(t.h, DWORD(warteMs));
        if (WaitForSingleObject(t.h, 0) == WAIT_OBJECT_0)
            weg << QStringLiteral("main.exe (PID %1)").arg(t.pid);
    };
    // Erst die Kinder: darin laeuft Python, sie halten die Ports.
    for (const Treffer& t : treffer)
        if (pids.contains(t.eltern))
            beenden(t);
    // Dann die Bootloader - die gehen nach dem Ende ihres Kindes von selbst und
    // raeumen dabei ihren _MEI-Ordner weg. Wer haengt, wird beendet.
    for (const Treffer& t : treffer) {
        if (pids.contains(t.eltern))
            continue;
        if (hatKind(t.pid) && WaitForSingleObject(t.h, DWORD(warteMs)) == WAIT_OBJECT_0)
            weg << QStringLiteral("main.exe (PID %1)").arg(t.pid);
        else
            beenden(t);
    }
    for (const Treffer& t : treffer)
        CloseHandle(t.h);
#else
    Q_UNUSED(pfade);
    Q_UNUSED(warteMs);
#endif
    return weg;
}

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
    hud::applyAccessKey(req);
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
    if (!m_server.isRunning()) {
        // Nach dem Update von 0.2.x laeuft dessen Server noch und haelt die Ports.
        const QStringList weg = alteServerBeenden(m_alteServer);
        if (!weg.isEmpty())
            qInfo().noquote() << "[UPDATE] Server der Python-Fassung beendet:" << weg.join(QStringLiteral(", "));
        bool ok = m_server.start(m_httpPort, m_udpPort, m_bind);
        // Gerade beendet: Windows gibt die Ports nicht immer im selben Augenblick frei.
        for (int i = 0; !ok && !weg.isEmpty() && i < 4; ++i) {
            QThread::msleep(250);
            ok = m_server.start(m_httpPort, m_udpPort, m_bind);
        }
        if (!ok)
            emit errorOccurred(m_server.errorString());
    }
    announce();
}

void LocalServer::stop()
{
    m_server.stop();
    announce();
}

server::AccessGuard* LocalServer::access()
{
    return &m_server.access();
}

quint16 LocalServer::httpPort() const
{
    return m_server.isRunning() ? m_server.httpPort() : m_httpPort;
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
