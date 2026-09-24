// Kamera-Tasten aus der Regie: eine Position -> Tastendruck ins Spiel.
//
// Die Spectator-Fahrerauswahl liegt auf 1-0 (P1-P10) und Shift+1-0 (P11-P20).
// P21/P22 = auf P20 springen und mit der "naechster Fahrer"-Taste weiter.
//
// ⚠ Die Tabelle steht HIER. Der Endpunkt bekommt nur eine Positionsnummer - von
// aussen laesst sich keine beliebige Taste ins Spiel schicken.
//
// Gesendet wird per SendInput mit SCANCODES (wie pydirectinput): normale
// SendInput-Aufrufe mit virtuellen Tastencodes ignoriert ein DirectInput-Spiel.
// Die Taste wird GEHALTEN (camholdms, Standard 60 ms) - ein Spiel liest die
// Tastatur einmal je Bild, ein nur angetippter Druck faellt zwischen zwei
// Abfragen durch. Anders als pydirectinput blockiert das Halten hier nichts:
// die Schritte laufen ueber einen Timer.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace kers::server {

class CamKeys : public QObject {
    Q_OBJECT
public:
    /// (Scancode, erweitert, loslassen) -> gesendet?
    using Sender = std::function<bool(quint16 scan, bool extended, bool keyUp)>;

    explicit CamKeys(QObject* parent = nullptr);

    /// Tastenfolge fuer eine Position (1..22). Leer = nicht moeglich (P21/22 ohne Weiter-Taste).
    static QStringList folgeFuer(int pos, const QString& weiter);
    /// Scancode einer erlaubten Taste ("f8", "shift", "left" ...). false = unbekannt.
    static bool scancode(const QString& taste, quint16& scan, bool& extended);

    /// Folge in die Warteschlange; "" = ok, sonst Fehlertext.
    QString druecke(const QStringList& folge, int haltenMs);
    bool busy() const { return !m_steps.isEmpty(); }

    /// Fuer Tests: statt SendInput.
    void setSender(Sender s) { m_send = std::move(s); }

private:
    struct Step {
        int waitMs = 0;          // > 0: Pause
        quint16 scan = 0;
        bool ext = false;
        bool up = false;
    };
    void run();

    Sender m_send;
    QList<Step> m_steps;
    QTimer m_timer;
};

}  // namespace kers::server
