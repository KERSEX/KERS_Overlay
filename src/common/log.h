// Das Log - Ersatz fuer das Konsolenfenster des Python-Servers.
//
// Die EXE laeuft ohne Konsole (WIN32). Alles, was frueher per print() in der
// Server-Konsole oder ueber pythonw ins Nichts ging, laeuft hier ueber
// qInfo()/qWarning() zusammen:
//
//   * Datei data\kers.log (bei jedem Start neu, damit nur der FRISCHE Lauf drinsteht)
//   * Ringpuffer der letzten Zeilen fuer die Log-Ansicht im Schaltbrett
//   * Signal fuer jede neue Zeile, damit die Ansicht live mitlaeuft
//   * Debugger-Ausgabe (OutputDebugString), falls jemand mit DebugView zuschaut
//
// QML-Warnungen (unbekannte Property, fehlgeschlagenes Binding) landen ebenfalls
// hier - das sind die wichtigsten Hinweise beim Portieren der Szene.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace kers::log {

class Sink : public QObject {
    Q_OBJECT
public:
    static Sink* instance();
    QStringList recent() const;      // Kopie des Ringpuffers, aelteste zuerst
signals:
    void line(const QString& text);
private:
    friend void install(const QString&);
    friend void append(const QString&);
    Sink() = default;
};

/// Einmal beim Start: Message-Handler setzen, Logdatei anlegen.
void install(const QString& logFilePath);

/// Eine Zeile ins Log (ohne Umweg ueber qInfo), z.B. fuer Serverzeilen.
void append(const QString& text);

}  // namespace kers::log
