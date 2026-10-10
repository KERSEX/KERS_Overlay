#include "access_guard.h"

#include "http_server.h"
#include "jsonfile.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace kers::server {

namespace {

qint64 wandUhr()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// Host-Header ohne Port, klein. "[::1]:5100" -> "::1"
QString hostOhnePort(const QByteArray& host)
{
    QString h = QString::fromLatin1(host).trimmed().toLower();
    if (h.startsWith(QLatin1Char('['))) {
        const int zu = h.indexOf(QLatin1Char(']'));
        return zu > 0 ? h.mid(1, zu - 1) : h;
    }
    const int doppel = h.lastIndexOf(QLatin1Char(':'));
    if (doppel > 0 && h.indexOf(QLatin1Char(':')) == doppel)   // genau ein ':' -> Port
        h = h.left(doppel);
    return h;
}

}  // namespace

AccessGuard::AccessGuard(const QString& file, QObject* parent)
    : QObject(parent)
    , m_file(file)
    , m_clock(&wandUhr)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &AccessGuard::save);
    load();
}

AccessGuard::~AccessGuard()
{
    if (m_saveTimer.isActive())
        save();
}

// ── Wer fragt? ───────────────────────────────────────────────────────────────
QString AccessGuard::ipText(const QHostAddress& a)
{
    bool ok = false;
    const quint32 v4 = a.toIPv4Address(&ok);           // auch ::ffff:192.168.1.5
    if (ok)
        return QHostAddress(v4).toString();
    QHostAddress ohneScope(a);
    ohneScope.setScopeId(QString());
    return ohneScope.toString();
}

bool AccessGuard::isLocalAddress(const QHostAddress& a)
{
    if (a.isLoopback())
        return true;
    // Eigene Adressen (LAN, VPN ...) - wer die Regie auf diesem PC ueber die
    // LAN-IP oeffnet, ist trotzdem dieser PC. Kurz zwischenspeichern: die Liste
    // abzufragen kostet, und sie aendert sich selten.
    static QList<QHostAddress> eigene;
    static QElapsedTimer alter;
    if (!alter.isValid() || alter.elapsed() > 30000) {
        eigene = QNetworkInterface::allAddresses();
        alter.start();
    }
    const QString ip = ipText(a);
    for (const QHostAddress& e : std::as_const(eigene))
        if (ipText(e) == ip)
            return true;
    return false;
}

bool AccessGuard::isLocalRequest(const HttpRequest& req)
{
    if (!isLocalAddress(req.peer))
        return false;
    // ⚠ DNS-Rebinding: eine fremde Webseite laesst ihren Namen auf 127.0.0.1
    // zeigen und spricht dann aus dem Browser dieses PCs mit dem Server - dann
    // stimmen Origin und Host ueberein, und der Absender ist "lokal". Deshalb
    // zaehlt eine lokale Anfrage nur unter einem Namen, den niemand von aussen
    // umbiegen kann: IP, localhost oder der eigene Rechnername.
    const QString h = hostOhnePort(req.header("host"));
    if (h.isEmpty() || !QHostAddress(h).isNull())
        return true;
    if (h == QLatin1String("localhost") || h.endsWith(QLatin1String(".localhost")))
        return true;
    const QString pc = QHostInfo::localHostName().toLower();
    return !pc.isEmpty() && (h == pc || h.startsWith(pc + QLatin1Char('.')));
}

QByteArray AccessGuard::tokenFrom(const HttpRequest& req, bool* fromQuery) const
{
    *fromQuery = false;
    QByteArray t = req.header("x-kers-key").trimmed();
    if (!t.isEmpty())
        return t;
    for (const QByteArray& teil : req.header("cookie").split(';')) {
        const QByteArray s = teil.trimmed();
        if (s.startsWith(QByteArray(COOKIE) + '='))
            return s.mid(qstrlen(COOKIE) + 1);
    }
    t = req.arg(QStringLiteral("key")).trimmed().toLatin1();
    if (!t.isEmpty())
        *fromQuery = true;
    return t;
}

AccessGuard::Device* AccessGuard::deviceForToken(const QByteArray& token)
{
    if (token.isEmpty())
        return nullptr;
    const QByteArray h = hashToken(token);
    for (Device& d : m_devices)
        if (d.tokenHash == h)
            return &d;
    return nullptr;
}

AccessGuard::Result AccessGuard::check(const HttpRequest& req)
{
    Result r;
    const QString ip = ipText(req.peer);
    if (m_blockedIps.contains(ip)) {
        r.verdict = Verdict::Blocked;
        return r;
    }
    bool ausQuery = false;
    const QByteArray token = tokenFrom(req, &ausQuery);
    Device* d = deviceForToken(token);
    if (d && d->blocked) {
        r.verdict = Verdict::Blocked;
        r.deviceId = d->id;
        return r;
    }
    if (isLocalRequest(req)) {
        r.verdict = Verdict::Local;
        return r;
    }
    if (d) {
        const qint64 jetzt = m_clock();
        // "Zuletzt gesehen" nur grob nachfuehren - sonst schriebe jede Anfrage die Datei.
        if (jetzt - d->lastSeen > 60000 || d->ip != ip) {
            d->lastSeen = jetzt;
            d->ip = ip;
            saveLater();
            emit changed();
        }
        r.verdict = Verdict::Allowed;
        r.deviceId = d->id;
        r.keyFromQuery = ausQuery;
        r.token = token;
        return r;
    }
    if (!m_enabled) {
        r.verdict = Verdict::Allowed;
        return r;
    }
    // Schriften und Bilder der Login-Seite sind kein Versuch.
    if (!req.path.startsWith(QLatin1String("/static/")) && req.path != QLatin1String("/favicon.ico"))
        noteAttempt(ip, QString::fromUtf8(req.header("user-agent")));
    r.verdict = Verdict::Unknown;
    return r;
}

void AccessGuard::noteAttempt(const QString& ip, const QString& agent)
{
    const qint64 jetzt = m_clock();
    auto it = std::find_if(m_attempts.begin(), m_attempts.end(), [&](const Attempt& a) { return a.ip == ip; });
    if (it == m_attempts.end()) {
        m_attempts.prepend({ip, agent.left(160), jetzt, 1});
        while (m_attempts.size() > MAX_VERSUCHE_LISTE)
            m_attempts.removeLast();
    } else {
        it->count++;
        it->last = jetzt;
        if (!agent.isEmpty())
            it->agent = agent.left(160);
    }
    saveLater();
    if (!m_gemeldet.contains(ip)) {
        m_gemeldet.insert(ip);
        qInfo().noquote() << "[ZUGRIFF] Fremdes Geraet ohne Code:" << ip << deviceNameFromAgent(agent);
        emit attemptSeen(ip, agent);
        emit changed();
    }
}

// ── Anmelden ─────────────────────────────────────────────────────────────────
AccessGuard::Login AccessGuard::login(const QHostAddress& peer, const QString& code, const QString& name,
                                      const QString& agent, QByteArray* token, QString* deviceId, int* lockSeconds,
                                      int* rest)
{
    const QString ip = ipText(peer);
    if (m_blockedIps.contains(ip))
        return Login::Blocked;
    const qint64 jetzt = m_clock();
    Fehl& f = m_fehl[ip];
    if (f.lockedUntil > jetzt) {
        if (lockSeconds)
            *lockSeconds = static_cast<int>(std::ceil((f.lockedUntil - jetzt) / 1000.0));
        return Login::Locked;
    }
    QString eingabe = code;
    eingabe.remove(QLatin1Char(' '));
    if (m_code.isEmpty() || eingabe != m_code) {
        f.count++;
        if (f.count >= MAX_FEHLVERSUCHE) {
            f.count = 0;
            f.lockedUntil = jetzt + SPERRE_SEK * 1000LL;
            qWarning().noquote() << "[ZUGRIFF]" << ip << "-" << MAX_FEHLVERSUCHE << "falsche Codes, Pause"
                                 << SPERRE_SEK << "s";
            if (lockSeconds)
                *lockSeconds = SPERRE_SEK;
            noteAttempt(ip, agent);
            return Login::Locked;
        }
        if (rest)
            *rest = MAX_FEHLVERSUCHE - f.count;
        noteAttempt(ip, agent);
        return Login::Wrong;
    }
    m_fehl.remove(ip);

    Device d;
    d.id = QString::fromLatin1(QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0'));
    const QByteArray t = randomToken();
    d.tokenHash = hashToken(t);
    d.name = name.trimmed().left(40);
    if (d.name.isEmpty())
        d.name = deviceNameFromAgent(agent);
    d.ip = ip;
    d.firstSeen = d.lastSeen = jetzt;
    m_devices.append(d);
    m_attempts.erase(std::remove_if(m_attempts.begin(), m_attempts.end(), [&](const Attempt& a) { return a.ip == ip; }),
                     m_attempts.end());
    save();
    qInfo().noquote() << "[ZUGRIFF] Neues Geraet freigegeben:" << d.name << ip;
    emit deviceAdded(d.name, ip);
    emit changed();
    if (token)
        *token = t;
    if (deviceId)
        *deviceId = d.id;
    return Login::Ok;
}

// ── Schaltbrett ──────────────────────────────────────────────────────────────
void AccessGuard::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    save();
    qInfo().noquote() << "[ZUGRIFF] Code fuer andere Geraete" << (on ? "verlangt" : "AUS");
    if (on)
        emit accessRevoked(QString(), QString());     // fremde Verbindungen ohne Schluessel kappen
    emit changed();
}

void AccessGuard::newCode()
{
    m_code = randomCode();
    save();
    emit changed();
}

QStringList AccessGuard::blockedIps() const
{
    QStringList l(m_blockedIps.begin(), m_blockedIps.end());
    l.sort();
    return l;
}

QByteArray AccessGuard::createDevice(const QString& name, QString* deviceId)
{
    Device d;
    d.id = QString::fromLatin1(QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0'));
    const QByteArray t = randomToken();
    d.tokenHash = hashToken(t);
    d.name = name.trimmed().left(40);
    if (d.name.isEmpty())
        d.name = QStringLiteral("Link");
    d.firstSeen = d.lastSeen = m_clock();
    m_devices.append(d);
    save();
    emit changed();
    if (deviceId)
        *deviceId = d.id;
    return t;
}

bool AccessGuard::isDeviceBlocked(const QString& id) const
{
    for (const Device& d : m_devices)
        if (d.id == id)
            return d.blocked;
    return false;
}

void AccessGuard::setDeviceBlocked(const QString& id, bool blocked)
{
    for (Device& d : m_devices) {
        if (d.id != id || d.blocked == blocked)
            continue;
        d.blocked = blocked;
        save();
        qInfo().noquote() << "[ZUGRIFF] Geraet" << d.name << (blocked ? "gesperrt" : "freigegeben");
        if (blocked)
            emit accessRevoked(QString(), id);
        emit changed();
        return;
    }
}

void AccessGuard::removeDevice(const QString& id)
{
    const auto alt = m_devices.size();
    m_devices.erase(std::remove_if(m_devices.begin(), m_devices.end(), [&](const Device& d) { return d.id == id; }),
                    m_devices.end());
    if (m_devices.size() == alt)
        return;
    save();
    emit accessRevoked(QString(), id);               // ohne Eintrag gilt sein Schluessel nicht mehr
    emit changed();
}

void AccessGuard::setIpBlocked(const QString& ip, bool blocked)
{
    const QString sauber = ip.trimmed();
    if (sauber.isEmpty() || QHostAddress(sauber).isNull())
        return;
    if (blocked == m_blockedIps.contains(sauber))
        return;
    if (blocked)
        m_blockedIps.insert(sauber);
    else
        m_blockedIps.remove(sauber);
    save();
    qInfo().noquote() << "[ZUGRIFF] IP" << sauber << (blocked ? "gesperrt" : "freigegeben");
    if (blocked)
        emit accessRevoked(sauber, QString());
    emit changed();
}

void AccessGuard::clearAttempts()
{
    m_attempts.clear();
    m_gemeldet.clear();
    save();
    emit changed();
}

// ── Hilfen ───────────────────────────────────────────────────────────────────
QString AccessGuard::randomCode()
{
    return QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QLatin1Char('0'));
}

QByteArray AccessGuard::randomToken()
{
    QByteArray roh(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(roh.data()), roh.size() / 4);
    return roh.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray AccessGuard::hashToken(const QByteArray& token)
{
    return QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
}

QByteArray AccessGuard::cookieHeader(const QByteArray& token)
{
    return QByteArray(COOKIE) + '=' + token + "; Path=/; Max-Age=" + QByteArray::number(COOKIE_TAGE * 86400LL)
        + "; HttpOnly; SameSite=Lax";
}

QString AccessGuard::deviceNameFromAgent(const QString& agent)
{
    if (agent.isEmpty())
        return QStringLiteral("Unbekanntes Geraet");
    if (agent.contains(QLatin1String("OBS")))
        return QStringLiteral("OBS");
    QString system;
    if (agent.contains(QLatin1String("iPhone")))
        system = QStringLiteral("iPhone");
    else if (agent.contains(QLatin1String("iPad")))
        system = QStringLiteral("iPad");
    else if (agent.contains(QLatin1String("Android")))
        system = QStringLiteral("Android");
    else if (agent.contains(QLatin1String("Windows")))
        system = QStringLiteral("Windows-PC");
    else if (agent.contains(QLatin1String("Macintosh")))
        system = QStringLiteral("Mac");
    else if (agent.contains(QLatin1String("Linux")))
        system = QStringLiteral("Linux");
    QString browser;
    if (agent.contains(QLatin1String("Edg/")))
        browser = QStringLiteral("Edge");
    else if (agent.contains(QLatin1String("OPR/")))
        browser = QStringLiteral("Opera");
    else if (agent.contains(QLatin1String("Firefox/")))
        browser = QStringLiteral("Firefox");
    else if (agent.contains(QLatin1String("Chrome/")))
        browser = QStringLiteral("Chrome");
    else if (agent.contains(QLatin1String("Safari/")))
        browser = QStringLiteral("Safari");
    else if (agent.contains(QLatin1String("Qt")) || agent.contains(QLatin1String("Mozilla/5.0")) == false)
        browser = QStringLiteral("Programm");
    if (system.isEmpty() && browser.isEmpty())
        return QStringLiteral("Unbekanntes Geraet");
    if (system.isEmpty())
        return browser;
    if (browser.isEmpty())
        return system;
    return system + QStringLiteral(" · ") + browser;
}

// ── Datei ────────────────────────────────────────────────────────────────────
void AccessGuard::load()
{
    bool ok = false;
    const QJsonObject o = json::readObject(m_file, &ok);
    m_enabled = !o.contains(QLatin1String("enabled")) || o.value(QLatin1String("enabled")).toBool(true);
    m_code = o.value(QLatin1String("code")).toString();
    if (m_code.size() != 6 || !std::all_of(m_code.begin(), m_code.end(), [](QChar c) { return c.isDigit(); }))
        m_code.clear();
    for (const QJsonValue& v : o.value(QLatin1String("devices")).toArray()) {
        const QJsonObject j = v.toObject();
        Device d;
        d.id = j.value(QLatin1String("id")).toString();
        d.tokenHash = j.value(QLatin1String("token_sha256")).toString().toLatin1();
        d.name = j.value(QLatin1String("name")).toString();
        d.ip = j.value(QLatin1String("ip")).toString();
        d.firstSeen = static_cast<qint64>(j.value(QLatin1String("first_seen")).toDouble());
        d.lastSeen = static_cast<qint64>(j.value(QLatin1String("last_seen")).toDouble());
        d.blocked = j.value(QLatin1String("blocked")).toBool();
        if (!d.id.isEmpty() && d.tokenHash.size() == 64)
            m_devices.append(d);
    }
    for (const QJsonValue& v : o.value(QLatin1String("blocked_ips")).toArray()) {
        const QString ip = v.toString().trimmed();
        if (!QHostAddress(ip).isNull())
            m_blockedIps.insert(ip);
    }
    for (const QJsonValue& v : o.value(QLatin1String("attempts")).toArray()) {
        const QJsonObject j = v.toObject();
        Attempt a{j.value(QLatin1String("ip")).toString(), j.value(QLatin1String("agent")).toString(),
                  static_cast<qint64>(j.value(QLatin1String("last")).toDouble()), j.value(QLatin1String("count")).toInt()};
        if (!a.ip.isEmpty() && m_attempts.size() < MAX_VERSUCHE_LISTE)
            m_attempts.append(a);
    }
    if (m_code.isEmpty()) {
        m_code = randomCode();
        save();
    }
}

void AccessGuard::saveLater()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void AccessGuard::save()
{
    m_saveTimer.stop();
    QJsonArray devs;
    for (const Device& d : std::as_const(m_devices))
        devs.append(QJsonObject{{"id", d.id},
                                {"token_sha256", QString::fromLatin1(d.tokenHash)},
                                {"name", d.name},
                                {"ip", d.ip},
                                {"first_seen", static_cast<double>(d.firstSeen)},
                                {"last_seen", static_cast<double>(d.lastSeen)},
                                {"blocked", d.blocked}});
    QJsonArray att;
    for (const Attempt& a : std::as_const(m_attempts))
        att.append(QJsonObject{{"ip", a.ip}, {"agent", a.agent}, {"last", static_cast<double>(a.last)}, {"count", a.count}});
    const QJsonObject o{{"enabled", m_enabled},
                        {"code", m_code},
                        {"devices", devs},
                        {"blocked_ips", QJsonArray::fromStringList(blockedIps())},
                        {"attempts", att}};
    if (!json::writeObject(m_file, o))
        qWarning().noquote() << "[ZUGRIFF] access.json nicht schreibbar:" << m_file;
}

}  // namespace kers::server
