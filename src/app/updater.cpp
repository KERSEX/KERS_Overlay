#include "updater.h"

#include "paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>

#include <algorithm>

namespace kers::app {

namespace {

QString apiLatest() { return QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(Updater::REPO)); }
QString apiAlle() { return QStringLiteral("https://api.github.com/repos/%1/releases?per_page=100").arg(QLatin1String(Updater::REPO)); }

}  // namespace

// ── freie Funktionen (ohne Netz, pruefbar) ───────────────────────────────────
QList<int> alsZahlen(const QString& version)
{
    QList<int> out;
    for (const QString& t : version.trimmed().split(QLatin1Char('.'))) {
        bool ok = false;
        const int n = t.toInt(&ok);
        if (!ok)
            return {0, 0, 0};
        out << n;
    }
    return out;
}

int vergleiche(const QList<int>& a, const QList<int>& b)
{
    // Wie Tupel in Python: elementweise, das kuerzere ist bei Gleichstand kleiner
    for (int i = 0; i < std::min(a.size(), b.size()); ++i)
        if (a.at(i) != b.at(i))
            return a.at(i) < b.at(i) ? -1 : 1;
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

QString versionAusTag(const QString& tag)
{
    // ⚠ Der Punkt hinter dem V ist Absicht: das Release 0.1.0 heisst auf GitHub
    // "KERS_SubsystemsV.0.1.0". Ohne das optionale \.? fiele es still aus der Liste.
    static const QRegularExpression re(QStringLiteral("KERS_SubsystemsV\\.?(\\d+\\.\\d+\\.\\d+)"));
    const auto m = re.match(tag);
    return m.hasMatch() ? m.captured(1) : QString();
}

QList<ReleaseInfo> releasesAusJson(const QByteArray& json)
{
    QList<ReleaseInfo> liste;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject rel = v.toObject();
        if (rel.value("draft").toBool())
            continue;
        const QString version = versionAusTag(rel.value("tag_name").toString());
        if (version.isEmpty())
            continue;
        QJsonObject asset;
        for (const QJsonValue& a : rel.value("assets").toArray())
            if (a.toObject().value("name").toString() == QLatin1String(Updater::ASSET_NAME)) {
                asset = a.toObject();
                break;
            }
        if (asset.isEmpty())
            continue;
        ReleaseInfo r;
        r.version = version;
        r.zahlen = alsZahlen(version);
        r.size = static_cast<qint64>(asset.value("size").toDouble());
        r.digest = asset.value("digest").toString();
        r.url = asset.value("browser_download_url").toString();
        r.published = rel.value("published_at").toString().left(10);
        r.vorab = rel.value("prerelease").toBool();
        liste << r;
    }
    std::stable_sort(liste.begin(), liste.end(),
                     [](const ReleaseInfo& a, const ReleaseInfo& b) { return vergleiche(a.zahlen, b.zahlen) > 0; });
    return liste;
}

QStringList altlastenAufraeumen(const QString& exePath)
{
    // Das .bak ist die Rueckfallebene des Austauschs: bis hierher konnte niemand
    // wissen, ob die neue EXE ueberhaupt startet. Dass DIESER Code laeuft, ist der
    // Beweis - also darf die alte Fassung weg. Mit ihr das Helfer-Batch und ein
    // liegengebliebenes .new aus einem abgebrochenen Download.
    // ⚠ Bei 0.2.6 -> 0.3.0 ist das .bak die 243-MB-Python-EXE.
    QStringList weg;
    if (exePath.isEmpty())
        return weg;
    const QFileInfo exe(exePath);
    for (const QString& p : {exePath + QStringLiteral(".bak"), exePath + QStringLiteral(".new"),
                             exe.dir().filePath(QStringLiteral("_kers_update.bat"))}) {
        if (QFileInfo(p).isFile() && QFile::remove(p))
            weg << QFileInfo(p).fileName();
    }
    return weg;
}

QByteArray tauschSkript(const QString& exe, const QString& neu, const QString& bak, qint64 pid)
{
    // Eine laufende EXE kann sich unter Windows nicht selbst ueberschreiben. Das
    // Batch wartet, bis wir weg sind, schiebt die alte Fassung als .bak beiseite,
    // zieht die neue an ihren Platz und startet sie. Anders als bei der Python-
    // Fassung gibt es keinen Bootloader mehr - gewartet wird auf EINEN Prozess.
    const QString e = QDir::toNativeSeparators(exe), n = QDir::toNativeSeparators(neu), b = QDir::toNativeSeparators(bak);
    QString s;
    s += QStringLiteral("@echo off\r\n");
    s += QStringLiteral("chcp 65001 >nul\r\n");
    s += QStringLiteral("set /a versuche=0\r\n");
    s += QStringLiteral(":warten\r\n");
    s += QStringLiteral("set /a versuche+=1\r\n");
    s += QStringLiteral("if %versuche% gtr 40 goto tauschen\r\n");
    s += QStringLiteral("tasklist /FI \"PID eq %1\" 2>nul | find \"%1\" >nul\r\n").arg(pid);
    s += QStringLiteral("if not errorlevel 1 goto nochmal\r\n");
    s += QStringLiteral("goto tauschen\r\n");
    s += QStringLiteral(":nochmal\r\n");
    s += QStringLiteral("timeout /t 1 /nobreak >nul\r\n");
    s += QStringLiteral("goto warten\r\n");
    s += QStringLiteral(":tauschen\r\n");
    s += QStringLiteral("if exist \"%1\" del /q \"%1\"\r\n").arg(b);
    s += QStringLiteral("move /y \"%1\" \"%2\" >nul\r\n").arg(e, b);
    s += QStringLiteral("move /y \"%1\" \"%2\" >nul\r\n").arg(n, e);
    s += QStringLiteral("if errorlevel 1 move /y \"%1\" \"%2\" >nul\r\n").arg(b, e);
    s += QStringLiteral("timeout /t 1 /nobreak >nul\r\n");
    s += QStringLiteral("start \"\" \"%1\"\r\n").arg(e);
    s += QStringLiteral("del \"%~f0\"\r\n");
    // ⚠ CRLF und keine BOM: cmd.exe verschluckt bei LF-Zeilenenden Zeichen am Zeilenanfang.
    return s.toUtf8();
}

// ── Updater ──────────────────────────────────────────────────────────────────
bool Updater::hostErlaubt(const QString& host)
{
    // GitHub leitet von github.com auf den Objektspeicher um; alles andere waere ein fremder Server.
    static const QStringList ok = {"github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
    return ok.contains(host);
}

Updater::Updater(QObject* parent)
    : QObject(parent)
    , m_nam(this)
{
    qRegisterMetaType<QList<kers::app::ReleaseInfo>>();
}

QString Updater::exePath() const
{
    return QCoreApplication::applicationFilePath();
}

QNetworkReply* Updater::anfrage(const QString& url)
{
    QNetworkRequest req{QUrl(url)};
    // GitHub weist Anfragen ohne User-Agent ab.
    req.setRawHeader("User-Agent", "KERS-Subsystems/" + paths::appVersion().toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    return m_nam.get(req);
}

void Updater::check()
{
    QNetworkReply* reply = anfrage(apiLatest());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 404)
                emit failed(QStringLiteral("Kein Release gefunden - ist das Repository noch oeffentlich?"));
            else if (code == 403)
                emit failed(QStringLiteral("GitHub blockt gerade (zu viele Anfragen). Spaeter nochmal."));
            else
                emit failed(QStringLiteral("Suche fehlgeschlagen: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const QString version = versionAusTag(data.value("tag_name").toString());
        if (version.isEmpty()) {
            emit failed(QStringLiteral("Keine gueltige Version im Release-Tag."));
            return;
        }
        QJsonObject asset;
        for (const QJsonValue& a : data.value("assets").toArray())
            if (a.toObject().value("name").toString() == QLatin1String(ASSET_NAME))
                asset = a.toObject();
        if (asset.isEmpty()) {
            emit failed(QStringLiteral("Im Release fehlt %1.").arg(QLatin1String(ASSET_NAME)));
            return;
        }
        m_info = ReleaseInfo{version, alsZahlen(version), static_cast<qint64>(asset.value("size").toDouble()),
                             asset.value("digest").toString(), asset.value("browser_download_url").toString(),
                             data.value("published_at").toString().left(10), false};
        if (vergleiche(alsZahlen(version), alsZahlen(paths::appVersion())) > 0)
            emit updateAvailable(version, m_info.size, m_info.published);
        else
            emit upToDate();
    });
}

void Updater::releasesLaden()
{
    QNetworkReply* reply = anfrage(apiAlle());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit failed(code == 403 ? QStringLiteral("GitHub blockt gerade (zu viele Anfragen). Spaeter nochmal.")
                                    : QStringLiteral("Liste nicht abrufbar: %1").arg(reply->errorString()));
            return;
        }
        m_releases = releasesAusJson(reply->readAll());
        emit releasesGeladen(m_releases);
    });
}

QString Updater::waehlen(const QString& version)
{
    QString gesucht = version.trimmed();
    while (gesucht.startsWith(QLatin1Char('v')))
        gesucht.remove(0, 1);
    gesucht = gesucht.trimmed();
    for (const ReleaseInfo& r : m_releases) {
        if (r.version != gesucht)
            continue;
        if (r.version == paths::appVersion())
            return QStringLiteral("Version %1 laeuft bereits.").arg(gesucht);
        m_info = r;
        return QString();
    }
    QStringList alle;
    for (const ReleaseInfo& r : m_releases)
        alle << r.version;
    return QStringLiteral("Version '%1' gibt es nicht. Verfuegbar: %2").arg(gesucht, alle.join(QStringLiteral(", ")));
}

void Updater::download()
{
    if (m_reply)
        return;
    const QUrl url(m_info.url);
    if (!hostErlaubt(url.host())) {
        // Schuetzt davor, dass eine manipulierte Antwort auf einen fremden Server
        // zeigt und wir uns dessen Datei als Programm unterschieben.
        emit failed(QStringLiteral("Unerwartete Download-Adresse: %1").arg(url.host()));
        return;
    }
    m_file.setFileName(neueDatei());
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(QStringLiteral("%1 nicht anlegbar: %2").arg(QFileInfo(neueDatei()).fileName(), m_file.errorString()));
        return;
    }
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "KERS-Subsystems/" + paths::appVersion().toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam.get(req);
    // In die Datei streamen statt zu sammeln
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_reply && m_file.isOpen())
            m_file.write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &Updater::progress);
    connect(m_reply, &QNetworkReply::finished, this, &Updater::downloadFertig);
}

void Updater::downloadFertig()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (m_file.isOpen()) {
        m_file.write(reply->readAll());
        m_file.close();
    }
    reply->deleteLater();
    const QString ziel = neueDatei();
    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(ziel);
        emit failed(QStringLiteral("Download abgebrochen: %1").arg(reply->errorString()));
        return;
    }
    const QString fehler = pruefen(ziel);
    if (!fehler.isEmpty()) {
        QFile::remove(ziel);
        emit failed(fehler);
        return;
    }
    emit ready(ziel);
}

QString Updater::pruefen(const QString& pfad) const
{
    const QFileInfo info(pfad);
    if (!info.isFile())
        return QStringLiteral("Heruntergeladene Datei nicht lesbar.");
    if (m_info.size > 0 && info.size() != m_info.size)
        return QStringLiteral("Groesse passt nicht (%1 statt %2 Bytes) - der Download ist unvollstaendig.")
            .arg(info.size())
            .arg(m_info.size);
    // Ab 0.3.1 Pflicht: ohne SHA-256 von GitHub wird nichts getauscht. Bis 0.3.0
    // wurde die Pruefung dann still uebersprungen - eine EXE ohne Pruefsumme ist
    // aber genau die, bei der man nicht weiss, was man bekommt.
    if (!m_info.digest.startsWith(QLatin1String("sha256:")))
        return QStringLiteral("GitHub liefert fuer diese Fassung keine Pruefsumme - aus Sicherheitsgruenden nicht installiert.");
    QFile f(pfad);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Heruntergeladene Datei nicht lesbar.");
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    if (QString::fromLatin1(h.result().toHex()) != m_info.digest.mid(7).toLower())
        return QStringLiteral("Pruefsumme stimmt nicht - Datei verwerfen.");
    return QString();
}

QString Updater::tauschStarten()
{
    const QString exe = exePath(), neu = neueDatei();
    if (exe.isEmpty() || !QFileInfo(neu).isFile())
        return QStringLiteral("Es liegt keine heruntergeladene Fassung bereit.");
    const QString skript = QFileInfo(exe).dir().filePath(QStringLiteral("_kers_update.bat"));
    QFile f(skript);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Austausch nicht startbar: %1").arg(f.errorString());
    f.write(tauschSkript(exe, neu, exe + QStringLiteral(".bak"), QCoreApplication::applicationPid()));
    f.close();
    QProcess p;
    p.setProgram(QStringLiteral("cmd.exe"));
    p.setArguments({QStringLiteral("/c"), QDir::toNativeSeparators(skript)});
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= 0x08000000;                       // CREATE_NO_WINDOW
    });
#endif
    if (!p.startDetached())
        return QStringLiteral("Austausch nicht startbar: %1").arg(p.errorString());
    return QString();
}

}  // namespace kers::app
