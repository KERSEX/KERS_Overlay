// OBS-Steuerung ueber obs-websocket v5 (ab 0.3.1).
//
// Die Verbindung zu OBS haelt der KERS-Server, nicht der Browser: das OBS-Passwort
// bleibt auf diesem PC, und ein Handy mit der Regie braucht nur den KERS-Code.
// Die Regie bekommt den Zustand ueber /api/obs (und /api/obs/stream) und schickt
// Aktionen per POST /api/obs - aus einer FESTEN Liste (Szene, Quelle an/aus, Mute,
// Lautstaerke, Uebergang, Stream, Aufnahme). Beliebige OBS-Befehle reicht der
// Server nicht durch.
//
// Protokoll: Hello (op 0) -> Identify (op 1, ggf. mit Auth) -> Identified (op 2),
// danach Requests (op 6) mit Antwort (op 7) und Events (op 5).
// Auth = base64(sha256(base64(sha256(passwort + salt)) + challenge)).
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>

#include <functional>

namespace kers::server {

class ObsLink : public QObject {
    Q_OBJECT
public:
    struct Config {
        // Vorgabe AUS: KERS verbindet sich erst mit OBS, wenn man es einschaltet
        // (Regie oder /settings) - kein ungefragter Zugriff auf ein laufendes OBS.
        bool enabled = false;
        QString host = QStringLiteral("127.0.0.1");
        int port = 4455;
        QString password;
    };
    using Reply = std::function<void(bool ok, const QJsonObject& data, const QString& comment)>;

    /// configFile: data\obs.json (Passwort darin per DPAPI verschluesselt)
    explicit ObsLink(const QString& configFile, QObject* parent = nullptr);
    ~ObsLink() override;

    void start();                                 // mit dem KERS-Server
    void stop();
    bool isConnected() const { return m_identified; }

    Config config() const { return m_cfg; }
    /// Uebernehmen, speichern, neu verbinden. setPassword=false laesst das alte stehen.
    void setConfig(const Config& c, bool setPassword);
    /// Fuer /api/obs/config - OHNE Passwort.
    QJsonObject configJson() const;

    /// Alles fuer die Regie (/api/obs).
    QJsonObject state() const;
    /// Eine Aktion aus der Regie. Rueckgabe: Fehlertext, leer = abgeschickt.
    QString action(const QJsonObject& a);

    /// Eigene Browser-Docks aus der OBS-Konfiguration dieses PCs (user.ini/global.ini).
    static QJsonArray browserDocks(const QString& obsConfigDir = QString());
    /// Twitch-Kanal aus einer Chat-Adresse (popout/embed), sonst leer.
    static QString twitchChannel(const QString& url);
    static QString authString(const QString& password, const QString& salt, const QString& challenge);
    static QByteArray protect(const QByteArray& plain);      // DPAPI, Base64
    static QByteArray unprotect(const QByteArray& base64);

signals:
    /// Zustand geaendert (gebuendelt, hoechstens alle 100 ms).
    void stateChanged();

private:
    void connectNow();
    void onConnected();
    void onDisconnected();
    void onText(const QString& text);
    void send(int op, const QJsonObject& d);
    void request(const QString& type, const QJsonObject& data = {}, Reply cb = {});
    void onHello(const QJsonObject& d);
    void onIdentified();
    void onEvent(const QString& type, const QJsonObject& d);
    void loadScenes();
    void loadItems();
    void loadInputs();
    void loadInput(const QString& name, const QString& kind);
    void loadTransitions();
    void pollStatus();
    void changed();
    void loadConfig();
    void saveConfig();
    void reset();

    QString m_file;
    Config m_cfg;
    QWebSocket m_ws;
    QTimer m_reconnect;
    QTimer m_poll;
    QTimer m_debounce;
    bool m_running = false;
    bool m_identified = false;
    QString m_error;
    QString m_version;
    QHash<QString, Reply> m_pending;
    quint64 m_nextId = 1;
    int m_tick = 0;

    // Zustand
    QStringList m_scenes;                          // Reihenfolge wie in OBS (oben zuerst)
    QString m_program;
    QString m_preview;
    bool m_studio = false;
    QJsonArray m_items;                            // Quellen der Programmszene
    QMap<QString, QJsonObject> m_audio;            // Name -> {name, muted, db}
    QStringList m_transitions;
    QString m_transition;
    QJsonObject m_stream;
    QJsonObject m_record;
    QJsonObject m_stats;
};

}  // namespace kers::server
