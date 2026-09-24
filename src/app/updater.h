// Sucht neue Fassungen auf GitHub und tauscht die EXE gegen sich selbst aus.
// Portierung von hud/updater.py - Protokoll unveraendert, damit 0.2.6 auf die
// C++-Fassung aktualisieren kann und umgekehrt.
//
// Ablauf, bewusst in zwei Schritten:
//   check()     fragt das neueste Release ab und meldet nur, dass es etwas gibt
//   download()  laedt es - erst auf Knopfdruck. Nichts passiert ungefragt, damit
//               kein Update mitten in einen Stream faellt.
//
// Release-Tag "KERS_SubsystemsV<x.y.z>" (auch "V.0.1.0" - dort ist ein Punkt zu
// viel gerutscht), Asset "KERS_Subsystems.exe", Pruefung ueber Groesse und die
// SHA-256 aus der GitHub-API. Versionen werden als ZAHLEN verglichen.
#pragma once

#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkReply;

namespace kers::app {

struct ReleaseInfo {
    QString version;
    QList<int> zahlen;
    qint64 size = 0;
    QString digest;          // "sha256:abc..."
    QString url;
    QString published;       // JJJJ-MM-TT
    bool vorab = false;
};

/// "0.0.10" -> {0,0,10}; Unlesbares -> {0,0,0}
QList<int> alsZahlen(const QString& version);
/// Numerischer Vergleich: <0, 0, >0
int vergleiche(const QList<int>& a, const QList<int>& b);
/// Version aus einem Release-Tag oder ""
QString versionAusTag(const QString& tag);
/// Antwort von /releases -> Liste, neueste zuerst (ohne Entwuerfe, ohne unser Asset)
QList<ReleaseInfo> releasesAusJson(const QByteArray& json);
/// Reste eines Updates entfernen (.bak, .new, _kers_update.bat) - erst, wenn die
/// neue Fassung laeuft. Rueckgabe: entfernte Dateinamen.
QStringList altlastenAufraeumen(const QString& exePath);
/// Inhalt des Helfer-Batches, das nach dem Beenden tauscht (CRLF, ohne BOM).
QByteArray tauschSkript(const QString& exe, const QString& neu, const QString& bak, qint64 pid);

class Updater : public QObject {
    Q_OBJECT
public:
    static constexpr const char* REPO = "KERSEX/KERS_Overlay";
    static constexpr const char* ASSET_NAME = "KERS_Subsystems.exe";
    /// Ab hier kennen die Fassungen den Ordner data\ .
    static QList<int> ersteMitDatenordner() { return {0, 1, 1}; }
    static bool hostErlaubt(const QString& host);

    explicit Updater(QObject* parent = nullptr);

    /// Die laufende EXE (leer im Test).
    QString exePath() const;
    QString neueDatei() const { return exePath() + QStringLiteral(".new"); }

    void check();
    void releasesLaden();
    QList<ReleaseInfo> releases() const { return m_releases; }
    /// Eine Fassung aus der Liste scharfstellen; "" = ok, sonst Fehlertext.
    QString waehlen(const QString& version);
    void download();
    bool busy() const { return m_reply != nullptr; }
    /// Helfer starten, der nach dem Beenden die EXE ersetzt. "" = angestossen.
    QString tauschStarten();

signals:
    void updateAvailable(const QString& version, qint64 size, const QString& published);
    void upToDate();
    void progress(qint64 geladen, qint64 gesamt);
    void ready(const QString& pfad);
    void failed(const QString& text);
    void releasesGeladen(const QList<kers::app::ReleaseInfo>& liste);

private:
    QNetworkReply* anfrage(const QString& url);
    void downloadFertig();
    QString pruefen(const QString& pfad) const;

    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    QFile m_file;
    ReleaseInfo m_info;
    QList<ReleaseInfo> m_releases;
};

}  // namespace kers::app

Q_DECLARE_METATYPE(kers::app::ReleaseInfo)
