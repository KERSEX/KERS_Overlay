#include "obs_link.h"

#include "access_guard.h"
#include "jsonfile.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <type_traits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace kers::server {

namespace {

// Alle Ereignisgruppen ausser den Dauerfeuer-Events (Pegel, aktive Szene je Quelle).
constexpr int EVENTS_NORMAL = 0x7FF;
constexpr int CLOSE_AUTH_FAILED = 4009;

double db(const QJsonValue& v)
{
    // -inf (stumm geregelt) kommt als null an
    const double d = v.isDouble() ? v.toDouble() : -100.0;
    return std::isfinite(d) ? std::clamp(d, -100.0, 26.0) : -100.0;
}

}  // namespace

ObsLink::ObsLink(const QString& configFile, QObject* parent)
    : QObject(parent)
    , m_file(configFile)
{
    loadConfig();
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &ObsLink::connectNow);
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &ObsLink::pollStatus);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, &ObsLink::stateChanged);

    connect(&m_ws, &QWebSocket::connected, this, &ObsLink::onConnected);
    // Verbindung weg ODER gar nicht erst zustande gekommen (OBS aus): beides endet
    // in UnconnectedState - "disconnected" allein kaeme beim zweiten Fall nicht.
    connect(&m_ws, &QWebSocket::stateChanged, this, [this](QAbstractSocket::SocketState st) {
        if (st == QAbstractSocket::UnconnectedState)
            onDisconnected();
    });
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &ObsLink::onText);
}

ObsLink::~ObsLink()
{
    m_running = false;
    m_ws.disconnect(this);
    m_ws.abort();
}

// ── Verbindung ───────────────────────────────────────────────────────────────
void ObsLink::start()
{
    m_running = true;
    connectNow();
}

void ObsLink::stop()
{
    m_running = false;
    m_reconnect.stop();
    m_poll.stop();
    m_ws.abort();
    reset();
    changed();
}

void ObsLink::connectNow()
{
    if (!m_running || !m_cfg.enabled || m_ws.state() != QAbstractSocket::UnconnectedState)
        return;
    m_ws.open(QUrl(QStringLiteral("ws://%1:%2").arg(m_cfg.host).arg(m_cfg.port)));
}

void ObsLink::onConnected()
{
    // Jetzt kommt Hello von OBS - darauf antwortet onHello.
}

void ObsLink::onDisconnected()
{
    const bool war = m_identified;
    // ⚠ Nicht nur closeCode() == 4009 vergleichen: 4009 liegt ausserhalb des
    // Qt-Enums CloseCode, clang haelt den Vergleich fuer "immer falsch" und darf
    // ihn wegoptimieren. Deshalb ueber int - und zusaetzlich am Grund, den
    // obs-websocket mitschickt ("Authentication failed.").
    const int code = static_cast<int>(static_cast<std::underlying_type_t<QWebSocketProtocol::CloseCode>>(m_ws.closeCode()));
    const bool authFehler = code == CLOSE_AUTH_FAILED
        || m_ws.closeReason().contains(QLatin1String("Authentication"), Qt::CaseInsensitive);
    reset();
    if (authFehler)
        m_error = QStringLiteral("OBS lehnt das Passwort ab – in /settings unter OBS prüfen.");
    else if (m_error.isEmpty() || war)
        m_error = QStringLiteral("OBS nicht erreichbar (ws://%1:%2) – läuft OBS, ist unter Werkzeuge → "
                                 "WebSocket-Server-Einstellungen der Server an?")
                      .arg(m_cfg.host)
                      .arg(m_cfg.port);
    if (war)
        qInfo().noquote() << "[OBS] getrennt";
    if (m_running && m_cfg.enabled)
        m_reconnect.start(authFehler ? 30000 : 5000);
    changed();
}

void ObsLink::reset()
{
    m_identified = false;
    m_poll.stop();
    m_pending.clear();
    m_scenes.clear();
    m_program.clear();
    m_preview.clear();
    m_studio = false;
    m_items = QJsonArray();
    m_audio.clear();
    m_transitions.clear();
    m_transition.clear();
    m_stream = QJsonObject();
    m_record = QJsonObject();
    m_stats = QJsonObject();
}

void ObsLink::send(int op, const QJsonObject& d)
{
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op", op}, {"d", d}}).toJson(QJsonDocument::Compact)));
}

void ObsLink::request(const QString& type, const QJsonObject& data, Reply cb)
{
    const QString id = QString::number(m_nextId++);
    if (cb)
        m_pending.insert(id, std::move(cb));
    QJsonObject d{{"requestType", type}, {"requestId", id}};
    if (!data.isEmpty())
        d["requestData"] = data;
    send(6, d);
}

void ObsLink::onText(const QString& text)
{
    const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    const QJsonObject d = o.value("d").toObject();
    switch (o.value("op").toInt(-1)) {
    case 0:
        onHello(d);
        break;
    case 2:
        onIdentified();
        break;
    case 5:
        onEvent(d.value("eventType").toString(), d.value("eventData").toObject());
        break;
    case 7: {
        const Reply cb = m_pending.take(d.value("requestId").toString());
        const QJsonObject st = d.value("requestStatus").toObject();
        if (cb)
            cb(st.value("result").toBool(), d.value("responseData").toObject(), st.value("comment").toString());
        break;
    }
    default:
        break;
    }
}

QString ObsLink::authString(const QString& password, const QString& salt, const QString& challenge)
{
    const QByteArray secret =
        QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256).toBase64();
    return QString::fromLatin1(
        QCryptographicHash::hash(secret + challenge.toUtf8(), QCryptographicHash::Sha256).toBase64());
}

void ObsLink::onHello(const QJsonObject& d)
{
    QJsonObject identify{{"rpcVersion", 1}, {"eventSubscriptions", EVENTS_NORMAL}};
    if (d.contains("authentication")) {
        if (m_cfg.password.isEmpty()) {
            m_error = QStringLiteral("OBS verlangt ein Passwort – in /settings unter OBS eintragen.");
            m_ws.close();
            return;
        }
        const QJsonObject a = d.value("authentication").toObject();
        identify["authentication"] = authString(m_cfg.password, a.value("salt").toString(), a.value("challenge").toString());
    }
    m_version = d.value("obsWebSocketVersion").toString();
    send(1, identify);
}

void ObsLink::onIdentified()
{
    m_identified = true;
    m_error.clear();
    qInfo().noquote() << "[OBS] verbunden mit" << QStringLiteral("ws://%1:%2").arg(m_cfg.host).arg(m_cfg.port);
    loadScenes();
    loadInputs();
    loadTransitions();
    request(QStringLiteral("GetStudioModeEnabled"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (ok) {
            m_studio = r.value("studioModeEnabled").toBool();
            changed();
        }
    });
    request(QStringLiteral("GetVersion"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (ok)
            m_version = r.value("obsVersion").toString();
    });
    pollStatus();
    m_poll.start();
    changed();
}

// ── Zustand laden ────────────────────────────────────────────────────────────
void ObsLink::loadScenes()
{
    request(QStringLiteral("GetSceneList"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        QList<QPair<int, QString>> liste;
        for (const QJsonValue& v : r.value("scenes").toArray())
            liste.append({v.toObject().value("sceneIndex").toInt(), v.toObject().value("sceneName").toString()});
        // OBS zaehlt von unten - in der Szenenliste steht der hoechste Index oben.
        std::sort(liste.begin(), liste.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        m_scenes.clear();
        for (const auto& s : liste)
            m_scenes << s.second;
        const QString prog = r.value("currentProgramSceneName").toString();
        if (prog != m_program) {
            m_program = prog;
            loadItems();
        }
        m_preview = r.value("currentPreviewSceneName").toString();
        changed();
    });
}

void ObsLink::loadItems()
{
    const QString szene = m_program;
    if (szene.isEmpty()) {
        m_items = QJsonArray();
        return;
    }
    request(QStringLiteral("GetSceneItemList"), {{"sceneName", szene}}, [this, szene](bool ok, const QJsonObject& r, const QString&) {
        if (!ok || szene != m_program)
            return;
        QList<QJsonObject> liste;
        for (const QJsonValue& v : r.value("sceneItems").toArray())
            liste.append(v.toObject());
        std::sort(liste.begin(), liste.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("sceneItemIndex").toInt() > b.value("sceneItemIndex").toInt();
        });
        QJsonArray items;
        for (const QJsonObject& i : liste)
            items.append(QJsonObject{{"id", i.value("sceneItemId").toInt()},
                                     {"name", i.value("sourceName").toString()},
                                     {"enabled", i.value("sceneItemEnabled").toBool()}});
        m_items = items;
        changed();
    });
}

void ObsLink::loadInputs()
{
    request(QStringLiteral("GetInputList"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        m_audio.clear();
        // Welche Quelle Ton hat, sagt die Liste nicht - GetInputMute klappt nur bei denen.
        for (const QJsonValue& v : r.value("inputs").toArray())
            loadInput(v.toObject().value("inputName").toString(), v.toObject().value("inputKind").toString());
        changed();
    });
}

void ObsLink::loadInput(const QString& name, const QString& kind)
{
    request(QStringLiteral("GetInputMute"), {{"inputName", name}}, [this, name, kind](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        m_audio[name] = QJsonObject{{"name", name}, {"kind", kind}, {"muted", r.value("inputMuted").toBool()}, {"db", -100.0}};
        request(QStringLiteral("GetInputVolume"), {{"inputName", name}}, [this, name](bool ok2, const QJsonObject& r2, const QString&) {
            if (ok2 && m_audio.contains(name)) {
                m_audio[name]["db"] = db(r2.value("inputVolumeDb"));
                changed();
            }
        });
        changed();
    });
}

void ObsLink::loadTransitions()
{
    request(QStringLiteral("GetSceneTransitionList"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        m_transitions.clear();
        for (const QJsonValue& v : r.value("transitions").toArray())
            m_transitions << v.toObject().value("transitionName").toString();
        m_transition = r.value("currentSceneTransitionName").toString();
        changed();
    });
}

void ObsLink::pollStatus()
{
    if (!m_identified)
        return;
    request(QStringLiteral("GetStreamStatus"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        const QJsonObject s{{"active", r.value("outputActive").toBool()},
                            {"reconnecting", r.value("outputReconnecting").toBool()},
                            {"timecode", r.value("outputTimecode").toString().left(8)}};
        if (s != m_stream) {
            m_stream = s;
            changed();
        }
    });
    request(QStringLiteral("GetRecordStatus"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
        if (!ok)
            return;
        const QJsonObject s{{"active", r.value("outputActive").toBool()},
                            {"paused", r.value("outputPaused").toBool()},
                            {"timecode", r.value("outputTimecode").toString().left(8)}};
        if (s != m_record) {
            m_record = s;
            changed();
        }
    });
    if (++m_tick % 2 == 0) {
        request(QStringLiteral("GetStats"), {}, [this](bool ok, const QJsonObject& r, const QString&) {
            if (!ok)
                return;
            const QJsonObject s{{"cpu", std::round(r.value("cpuUsage").toDouble() * 10) / 10},
                                {"memory", std::round(r.value("memoryUsage").toDouble())},
                                {"fps", std::round(r.value("activeFps").toDouble() * 10) / 10},
                                {"render_skipped", r.value("renderSkippedFrames").toInt()},
                                {"render_total", r.value("renderTotalFrames").toInt()},
                                {"output_skipped", r.value("outputSkippedFrames").toInt()},
                                {"output_total", r.value("outputTotalFrames").toInt()}};
            if (s != m_stats) {
                m_stats = s;
                changed();
            }
        });
    }
}

void ObsLink::onEvent(const QString& type, const QJsonObject& d)
{
    if (type == QLatin1String("CurrentProgramSceneChanged")) {
        m_program = d.value("sceneName").toString();
        loadItems();
    } else if (type == QLatin1String("CurrentPreviewSceneChanged")) {
        m_preview = d.value("sceneName").toString();
    } else if (type == QLatin1String("SceneListChanged") || type == QLatin1String("SceneCreated")
               || type == QLatin1String("SceneRemoved") || type == QLatin1String("SceneNameChanged")) {
        loadScenes();
    } else if (type == QLatin1String("SceneItemEnableStateChanged")) {
        if (d.value("sceneName").toString() == m_program) {
            for (int i = 0; i < m_items.size(); ++i) {
                QJsonObject it = m_items.at(i).toObject();
                if (it.value("id").toInt() == d.value("sceneItemId").toInt()) {
                    it["enabled"] = d.value("sceneItemEnabled").toBool();
                    m_items.replace(i, it);
                }
            }
        }
    } else if (type == QLatin1String("SceneItemCreated") || type == QLatin1String("SceneItemRemoved")
               || type == QLatin1String("SceneItemListReindexed")) {
        if (d.value("sceneName").toString() == m_program)
            loadItems();
    } else if (type == QLatin1String("InputMuteStateChanged")) {
        const QString n = d.value("inputName").toString();
        if (m_audio.contains(n))
            m_audio[n]["muted"] = d.value("inputMuted").toBool();
    } else if (type == QLatin1String("InputVolumeChanged")) {
        const QString n = d.value("inputName").toString();
        if (m_audio.contains(n))
            m_audio[n]["db"] = db(d.value("inputVolumeDb"));
    } else if (type == QLatin1String("InputCreated") || type == QLatin1String("InputRemoved")
               || type == QLatin1String("InputNameChanged")) {
        loadInputs();
    } else if (type == QLatin1String("CurrentSceneTransitionChanged")) {
        m_transition = d.value("transitionName").toString();
    } else if (type == QLatin1String("StreamStateChanged") || type == QLatin1String("RecordStateChanged")) {
        pollStatus();
    } else if (type == QLatin1String("StudioModeStateChanged")) {
        m_studio = d.value("studioModeEnabled").toBool();
    } else {
        return;
    }
    changed();
}

void ObsLink::changed()
{
    if (!m_debounce.isActive())
        m_debounce.start();
}

// ── Aktionen aus der Regie ───────────────────────────────────────────────────
QString ObsLink::action(const QJsonObject& a)
{
    if (!m_identified)
        return QStringLiteral("OBS ist nicht verbunden");
    const QString typ = a.value("action").toString();
    auto melde = [this, typ](bool ok, const QJsonObject&, const QString& comment) {
        if (!ok)
            qWarning().noquote() << "[OBS]" << typ << "abgelehnt:" << comment;
    };
    if (typ == QLatin1String("scene") || typ == QLatin1String("preview")) {
        const QString name = a.value("name").toString();
        if (!m_scenes.contains(name))
            return QStringLiteral("Szene unbekannt");
        if (typ == QLatin1String("preview") && !m_studio)
            return QStringLiteral("Studio-Modus ist aus");
        request(typ == QLatin1String("scene") ? QStringLiteral("SetCurrentProgramScene") : QStringLiteral("SetCurrentPreviewScene"),
                {{"sceneName", name}}, melde);
        return QString();
    }
    if (typ == QLatin1String("studio_transition")) {
        if (!m_studio)
            return QStringLiteral("Studio-Modus ist aus");
        request(QStringLiteral("TriggerStudioModeTransition"), {}, melde);
        return QString();
    }
    if (typ == QLatin1String("item")) {
        const int id = a.value("id").toInt(-1);
        const bool gibts = std::any_of(m_items.begin(), m_items.end(),
                                       [id](const QJsonValue& v) { return v.toObject().value("id").toInt() == id; });
        if (!gibts)
            return QStringLiteral("Quelle unbekannt");
        request(QStringLiteral("SetSceneItemEnabled"),
                {{"sceneName", m_program}, {"sceneItemId", id}, {"sceneItemEnabled", a.value("enabled").toBool()}}, melde);
        return QString();
    }
    if (typ == QLatin1String("mute") || typ == QLatin1String("volume")) {
        const QString input = a.value("input").toString();
        if (!m_audio.contains(input))
            return QStringLiteral("Audioquelle unbekannt");
        if (typ == QLatin1String("mute"))
            request(QStringLiteral("SetInputMute"), {{"inputName", input}, {"inputMuted", a.value("muted").toBool()}}, melde);
        else
            request(QStringLiteral("SetInputVolume"), {{"inputName", input}, {"inputVolumeDb", db(a.value("db"))}}, melde);
        return QString();
    }
    if (typ == QLatin1String("transition")) {
        const QString name = a.value("name").toString();
        if (!m_transitions.contains(name))
            return QStringLiteral("Übergang unbekannt");
        request(QStringLiteral("SetCurrentSceneTransition"), {{"transitionName", name}}, melde);
        return QString();
    }
    const QString op = a.value("op").toString();
    if (typ == QLatin1String("stream")) {
        if (op == QLatin1String("start") || op == QLatin1String("stop")) {
            qInfo().noquote() << "[OBS] Stream" << op << "(aus der Regie)";
            request(op == QLatin1String("start") ? QStringLiteral("StartStream") : QStringLiteral("StopStream"), {}, melde);
            return QString();
        }
        return QStringLiteral("op: start oder stop");
    }
    if (typ == QLatin1String("record")) {
        static const QHash<QString, QString> befehle{{"start", "StartRecord"}, {"stop", "StopRecord"},
                                                     {"pause", "PauseRecord"}, {"resume", "ResumeRecord"}};
        if (!befehle.contains(op))
            return QStringLiteral("op: start, stop, pause oder resume");
        request(befehle.value(op), {}, melde);
        return QString();
    }
    return QStringLiteral("unbekannte Aktion");
}

// ── Zustand fuer die Regie ───────────────────────────────────────────────────
QJsonObject ObsLink::state() const
{
    QJsonArray audio;
    for (const QJsonObject& a : m_audio)
        audio.append(a);
    // Docks nur, wenn OBS auf DIESEM PC laeuft - nur dann liegt seine Konfiguration hier.
    static QJsonArray docks;
    static QElapsedTimer docksAlter;
    const QString host = m_cfg.host.trimmed().toLower();
    const bool lokal = host == QLatin1String("localhost") || AccessGuard::isLocalAddress(QHostAddress(host));
    if (lokal && (!docksAlter.isValid() || docksAlter.elapsed() > 10000)) {
        docks = browserDocks();
        docksAlter.start();
    }
    return QJsonObject{{"enabled", m_cfg.enabled},
                       {"connected", m_identified},
                       {"error", m_error},
                       {"version", m_version},
                       {"scenes", QJsonArray::fromStringList(m_scenes)},
                       {"program", m_program},
                       {"preview", m_preview},
                       {"studio", m_studio},
                       {"items", m_items},
                       {"inputs", audio},
                       {"transitions", QJsonArray::fromStringList(m_transitions)},
                       {"transition", m_transition},
                       {"stream", m_stream},
                       {"record", m_record},
                       {"stats", m_stats},
                       {"docks", lokal ? docks : QJsonArray()},
                       {"docks_local", lokal}};
}

// ── OBS-Browser-Docks ────────────────────────────────────────────────────────
QString ObsLink::twitchChannel(const QString& url)
{
    static const QRegularExpression re(
        QStringLiteral("twitch\\.tv/(?:popout/|embed/)?([A-Za-z0-9_]{3,25})/chat"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(url);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QJsonArray ObsLink::browserDocks(const QString& obsConfigDir)
{
    const QString dir = obsConfigDir.isEmpty() ? qEnvironmentVariable("APPDATA") + QStringLiteral("/obs-studio") : obsConfigDir;
    // Ab OBS 31 in user.ini, davor in global.ini - Abschnitt [BasicWindow].
    // ⚠ Nicht ueber QSettings lesen: der Wert ist JSON mit Kommas, QSettings
    // zerlegt ihn in eine Liste.
    for (const char* name : {"user.ini", "global.ini"}) {
        QFile f(QDir(dir).filePath(QLatin1String(name)));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> zeilen = f.readAll().split('\n');
        for (const QByteArray& roh : zeilen) {
            const QByteArray z = roh.trimmed();
            if (!z.startsWith("ExtraBrowserDocks="))
                continue;
            const QJsonArray liste = QJsonDocument::fromJson(z.mid(18)).array();
            QJsonArray out;
            for (const QJsonValue& v : liste) {
                const QJsonObject d = v.toObject();
                const QString url = d.value("url").toString().trimmed();
                if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://")))
                    continue;
                const QString ch = twitchChannel(url);
                out.append(QJsonObject{{"title", d.value("title").toString().left(60)},
                                       {"url", url},
                                       {"twitch", ch.isEmpty() ? QJsonValue() : QJsonValue(ch)}});
            }
            return out;
        }
    }
    return {};
}

// ── Einstellungen ────────────────────────────────────────────────────────────
QJsonObject ObsLink::configJson() const
{
    return {{"enabled", m_cfg.enabled}, {"host", m_cfg.host}, {"port", m_cfg.port},
            {"has_password", !m_cfg.password.isEmpty()}};
}

void ObsLink::setConfig(const Config& c, bool setPassword)
{
    const QString altesPw = m_cfg.password;
    m_cfg = c;
    if (!setPassword)
        m_cfg.password = altesPw;
    if (m_cfg.host.trimmed().isEmpty())
        m_cfg.host = QStringLiteral("127.0.0.1");
    m_cfg.port = std::clamp(m_cfg.port, 1, 65535);
    saveConfig();
    m_reconnect.stop();
    m_ws.abort();
    reset();
    m_error.clear();
    if (m_running && m_cfg.enabled)
        QTimer::singleShot(0, this, &ObsLink::connectNow);
    changed();
}

void ObsLink::loadConfig()
{
    bool ok = false;
    const QJsonObject o = json::readObject(m_file, &ok);
    if (!ok)
        return;
    m_cfg.enabled = o.value("enabled").toBool(false);
    const QString host = o.value("host").toString().trimmed();
    if (!host.isEmpty())
        m_cfg.host = host;
    const int port = o.value("port").toInt(4455);
    m_cfg.port = (port >= 1 && port <= 65535) ? port : 4455;
    const QByteArray pw = o.value("password").toString().toLatin1();
    if (!pw.isEmpty())
        m_cfg.password = QString::fromUtf8(unprotect(pw));
}

void ObsLink::saveConfig()
{
    QJsonObject o{{"enabled", m_cfg.enabled}, {"host", m_cfg.host}, {"port", m_cfg.port}};
    if (!m_cfg.password.isEmpty())
        o["password"] = QString::fromLatin1(protect(m_cfg.password.toUtf8()));
    if (!json::writeObject(m_file, o))
        qWarning().noquote() << "[OBS] obs.json nicht schreibbar:" << m_file;
}

QByteArray ObsLink::protect(const QByteArray& plain)
{
#ifdef Q_OS_WIN
    // DPAPI: nur dieses Windows-Konto kann es wieder lesen - die Datei allein
    // verraet das OBS-Passwort nicht.
    DATA_BLOB ein{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB aus{};
    if (!CryptProtectData(&ein, L"KERS OBS", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &aus))
        return {};
    const QByteArray r(reinterpret_cast<const char*>(aus.pbData), static_cast<int>(aus.cbData));
    LocalFree(aus.pbData);
    return r.toBase64();
#else
    return plain.toBase64();
#endif
}

QByteArray ObsLink::unprotect(const QByteArray& base64)
{
    const QByteArray roh = QByteArray::fromBase64(base64);
#ifdef Q_OS_WIN
    DATA_BLOB ein{static_cast<DWORD>(roh.size()), reinterpret_cast<BYTE*>(const_cast<char*>(roh.constData()))};
    DATA_BLOB aus{};
    if (!CryptUnprotectData(&ein, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &aus))
        return {};
    const QByteArray r(reinterpret_cast<const char*>(aus.pbData), static_cast<int>(aus.cbData));
    LocalFree(aus.pbData);
    return r;
#else
    return roh;
#endif
}

}  // namespace kers::server
