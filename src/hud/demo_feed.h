// Ein erfundenes Rennen - fuers Bauen am Overlay ohne laufendes Spiel.
// Portierung von hud/demo.py.
//
// Der Demo-Feed erzeugt dieselben Payloads wie /api/stream, nur ausgedacht:
// 22 Fahrer (11 Teams wie ab 2026), die sich ueberholen, Boxenstopps, Schaden,
// Strafen, Rennleitungs-Meldungen, Kamerawechsel, eine erfundene Strecke und im
// Wechsel Safety Car, VSC und rote Flagge.
//
//   KERS_Subsystems.exe --demo          statt am Server zu haengen
//   Vorschau (Layout bearbeiten): kein Chart (laege bildschirmfuellend ueber
//   allem), WM-Stand dagegen dauerhaft an, damit er sich platzieren laesst.
#pragma once

#include "feed.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTimer>

namespace kers::hud {

class DemoFeed : public Feed {
    Q_OBJECT
public:
    DemoFeed(int hz = 30, bool quali = false, bool vorschau = false, QObject* parent = nullptr);

    void start() override;
    void stop() override;
    void setHz(int hz) override;
    bool hasOwnExtras() const override { return true; }
    QJsonArray trackPoints() const override { return m_track; }
    QJsonObject championship() const override;

private:
    struct Driver {
        int index;
        QString name, team;
        double pace, progress;
        QString compound;
        int tyreAge, grid;
        double pitUntil = 0.0;
        int stops = 0;
        double best = 0.0, last = 0.0;
        int lastLapNum = -1;
    };
    void emitPayload();
    QJsonArray raceControl(double t, bool redFlag, const QList<const Driver*>& order);
    static QString demoChart(double t);
    static QJsonArray buildTrack(int n = 600);
    QJsonArray trackPoint(double frac) const;
    static QJsonObject realSettings();

    bool m_quali;
    bool m_vorschau;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QList<Driver> m_drivers;
    QJsonArray m_track;
    QJsonObject m_settings;
    int m_rcId = 0;
    double m_rcNext = 12.0;        // erste Rennleitungs-Meldung nach 12 s
};

}  // namespace kers::hud
