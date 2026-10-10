// Zugriffsschutz fuer alles, was nicht von diesem PC kommt (ab 0.3.1).
//
// Bis 0.3.0 konnte jedes Geraet im LAN alles: Einstellungen aendern, Aufnahmen
// starten und ueber /api/cam Tasten auf dem Spiele-PC druecken. Jetzt gilt:
//
//   * Dieser PC (Loopback und eigene Adressen) ist frei - OBS, HUD, Regie im
//     Browser nebenan brauchen nichts.
//   * Jedes andere Geraet gibt einmal den Code aus dem Schaltbrett ein und
//     bekommt dafuer einen Schluessel (Cookie, Header X-KERS-Key oder ?key=).
//   * Geraete und IPs lassen sich sperren; die Sperre steht in data\access.json
//     und gilt auch nach einem Neustart - und auch, wenn der Schutz aus ist.
//
// Gespeichert wird nur der SHA-256 eines Schluessels, nie der Schluessel selbst.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>

namespace kers::server {

struct HttpRequest;

class AccessGuard : public QObject {
    Q_OBJECT
public:
    static constexpr int MAX_FEHLVERSUCHE = 5;       // je IP, dann Pause
    static constexpr int SPERRE_SEK = 60;
    static constexpr int MAX_VERSUCHE_LISTE = 30;     // "Unbekannte Versuche" im Schaltbrett
    static constexpr int COOKIE_TAGE = 400;
    static constexpr const char* COOKIE = "kers_dev";

    struct Device {
        QString id;
        QByteArray tokenHash;                         // SHA-256 (hex) des Schluessels
        QString name;
        QString ip;                                   // zuletzt gesehen
        qint64 firstSeen = 0;                         // ms seit Epoche
        qint64 lastSeen = 0;
        bool blocked = false;
    };
    struct Attempt {
        QString ip;
        QString agent;
        qint64 last = 0;
        int count = 0;
    };

    enum class Verdict {
        Local,     // dieser PC
        Allowed,   // Geraet mit gueltigem Schluessel - oder Schutz aus
        Blocked,   // Geraet oder IP gesperrt
        Unknown,   // fremd, ohne (gueltigen) Schluessel
    };
    struct Result {
        Verdict verdict = Verdict::Unknown;
        QString deviceId;                             // bei Allowed mit Schluessel
        bool keyFromQuery = false;                    // ?key= -> Cookie setzen
        QByteArray token;                             // der vorgezeigte Schluessel (fuer das Cookie)
    };

    enum class Login { Ok, Wrong, Locked, Blocked };

    /// file: data\access.json
    explicit AccessGuard(const QString& file, QObject* parent = nullptr);
    ~AccessGuard() override;

    /// Wer fragt hier? Aktualisiert "zuletzt gesehen" und merkt sich fremde Versuche.
    Result check(const HttpRequest& req);

    /// Code pruefen. Bei Ok liegt in *token der neue Schluessel, in *deviceId das Geraet.
    /// lockSeconds: bei Locked die restliche Wartezeit.
    Login login(const QHostAddress& peer, const QString& code, const QString& name, const QString& agent,
                QByteArray* token, QString* deviceId = nullptr, int* lockSeconds = nullptr, int* rest = nullptr);

    /// Kommt die Anfrage von diesem PC - und unter einem Namen, den ein Angreifer
    /// nicht per DNS umbiegen kann (IP, localhost, Rechnername)?
    static bool isLocalRequest(const HttpRequest& req);
    static bool isLocalAddress(const QHostAddress& a);
    static QString ipText(const QHostAddress& a);
    static QString deviceNameFromAgent(const QString& agent);
    static QByteArray cookieHeader(const QByteArray& token);
    static QByteArray hashToken(const QByteArray& token);

    // ── Fuers Schaltbrett ────────────────────────────────────────────────────
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString code() const { return m_code; }
    void newCode();
    QList<Device> devices() const { return m_devices; }
    QList<Attempt> attempts() const { return m_attempts; }
    QStringList blockedIps() const;
    /// Geraet ohne Code anlegen (Link fuer OBS / zweiten PC). Rueckgabe: Schluessel.
    QByteArray createDevice(const QString& name, QString* deviceId = nullptr);
    void setDeviceBlocked(const QString& id, bool blocked);
    void removeDevice(const QString& id);
    void setIpBlocked(const QString& ip, bool blocked);
    void clearAttempts();
    bool isIpBlocked(const QString& ip) const { return m_blockedIps.contains(ip); }
    bool isDeviceBlocked(const QString& id) const;

    /// Uhr (ms seit Epoche) - fuer Tests austauschbar.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

signals:
    /// Irgendetwas an Geraeten, Code oder Schalter hat sich geaendert.
    void changed();
    void deviceAdded(const QString& name, const QString& ip);
    /// Erstmals in dieser Sitzung: ein fremdes Geraet ohne Schluessel klopft an.
    void attemptSeen(const QString& ip, const QString& agent);
    /// Gesperrt - offene Verbindungen dieser IP bzw. dieses Geraets schliessen.
    void accessRevoked(const QString& ip, const QString& deviceId);

private:
    void load();
    void save();
    void saveLater();
    Device* deviceForToken(const QByteArray& token);
    QByteArray tokenFrom(const HttpRequest& req, bool* fromQuery) const;
    void noteAttempt(const QString& ip, const QString& agent);
    static QString randomCode();
    static QByteArray randomToken();

    QString m_file;
    std::function<qint64()> m_clock;
    bool m_enabled = true;
    QString m_code;
    QList<Device> m_devices;
    QList<Attempt> m_attempts;
    QSet<QString> m_blockedIps;
    QSet<QString> m_gemeldet;                         // Versuche schon gemeldet (diese Sitzung)
    struct Fehl {
        int count = 0;
        qint64 lockedUntil = 0;
    };
    QHash<QString, Fehl> m_fehl;                      // Fehlversuche je IP (nur im Speicher)
    QTimer m_saveTimer;
};

}  // namespace kers::server
