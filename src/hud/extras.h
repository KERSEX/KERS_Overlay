// Die drei Bausteine mit eigenen Server-Abfragen - Portierung von hud/extras.py.
//
//     Trackmap       /api/track          die live gelernte Streckenkontur
//     Charts         /api/lap_positions  Positionen je Runde (Paket 15)
//     Championship   /api/championship   WM-Punkte plus Live-Hochrechnung
//
// ⚠ WARUM DIE GEOMETRIE HIER GERECHNET WIRD: die Kontur hat auf einer 5-km-
// Strecke gut tausend Punkte, und jeder muss skaliert, gedreht und gespiegelt
// werden. In QML-JavaScript hiesse das tausend Punkte pro Bild durch den
// Interpreter. Hier passiert es einmal - und nur neu, wenn sich Kontur, Drehung
// oder Spiegelung aendern. QML bekommt fertige Punktlisten.
#pragma once

#include "data_source.h"
#include "derive.h"
#include "kers_prop.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace kers::hud {

/// Die Minimap (trackmap.js). Punkte sind [x, z, Rundenanteil, Sektor].
class Trackmap : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(int, size, 400)
    Q_PROPERTY(QVariantList contour READ contour NOTIFY contourChanged)
    Q_PROPERTY(QVariantList flags READ flags NOTIFY contourChanged)
    Q_PROPERTY(QVariantList cars READ cars NOTIFY carsChanged)
public:
    static constexpr int POLL_MS = 2000;
    static constexpr double STOPPED_MS = 1500;   // so lange ohne Bewegung gilt ein Auto als "steht"
    static constexpr double STOPPED_M = 3;       // ... bei weniger als dieser Strecke
    static constexpr double FIT_TARGET = 88.0;   // wieviel vom 0..100-Feld die Strecke einnehmen darf
    static constexpr double DNF_LINGER_MS = 12000;

    Trackmap(Config& cfg, DataSource* source, QObject* parent = nullptr);

    QVariantList contour() const { return m_contour; }
    QVariantList flags() const { return m_flags; }
    QVariantList cars() const { return m_cars; }

    void setSource(DataSource* source) { m_source = source; }
    void start();
    void stop();
    /// Kontur direkt setzen, ohne den Server zu fragen (Demo-Modus).
    void loadPoints(const QJsonArray& pts, int ver = 1);
    void update(const QList<QJsonObject>& drivers, int focusIndex, bool connected,
                const QJsonObject& session, bool enabled, double nowMs);

signals:
    void contourChanged();
    void carsChanged();

private:
    struct Pt {
        double x = 0, z = 0, frac = -1;
        int sector = 0;
        int n = 2;       // wie viele Felder der Punkt urspruenglich hatte (2..4)
    };
    struct Bounds {
        double minX, minZ, sc, offX, offZ;
    };
    struct Ghost {
        double t, x, z;
        QString color;
    };
    void refresh();
    void onTrack(const QJsonObject& t);
    bool ensureTransform(bool force = false);
    void updateFit();
    QPointF rotated(double x, double z) const;
    std::optional<QPointF> norm(double x, double z) const;
    void buildContour();
    void buildFlags(const QJsonArray& zones);
    static QList<Pt> smoothLoop(const QList<Pt>& pts, int passes = 2);

    Config& m_cfg;
    DataSource* m_source;
    QTimer m_timer;
    QList<Pt> m_pts;
    std::optional<Bounds> m_bounds;
    int m_ver = -1;
    bool m_done = false;
    double m_cos = 1.0, m_sin = 0.0;
    bool m_flip = true;
    double m_fitF = 1.0, m_fitCx = 50.0, m_fitCz = 50.0;
    QString m_tfSig;
    QVariantList m_contour;
    QVariantList m_flags;
    QString m_flagSig;
    QVariantList m_cars;
    struct Move { double x, z, t; };
    QHash<int, Move> m_move;
    QHash<int, Ghost> m_ghost;
    QSet<int> m_ghostDone;
    QHash<int, QPointF> m_lastPos;
};

/// Positions-, Gap- und Rundenzeiten-Verlauf (charts.js). Ueber /regie eingeschaltet.
class Charts : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, title, QString())
    KERS_PROP(QString, mode, QString())
    KERS_PROP(int, maxLap, 0)
    KERS_PROP(double, maxValue, 0.0)
    KERS_PROP(bool, zeroBased, false)
    KERS_PROP(QString, emptyText, QString())
    Q_PROPERTY(QVariantList series READ series NOTIFY seriesChanged)
public:
    static constexpr double REDRAW_MS = 3000;

    Charts(DataSource* source, SharedState& shared, QObject* parent = nullptr);
    QVariantList series() const { return m_series; }
    void setSource(DataSource* source) { m_source = source; }
    void update(const QString& mode, double nowMs);

signals:
    void seriesChanged();

private:
    struct Meta { QString name, team; };
    using Laps = QMap<int, QHash<int, double>>;
    void onPositions(const QJsonObject& data);
    QHash<int, Meta> meta() const;
    Laps lapDeltas() const;
    static Laps normalize(const QJsonObject& lapsObj);
    void build(const Laps& data, const QHash<int, Meta>& meta, bool zeroBased);

    DataSource* m_source;
    SharedState& m_shared;
    QVariantList m_series;
    std::optional<QJsonObject> m_positions;   // letzte Antwort von /api/lap_positions
    double m_pending = 0.0;
};

/// WM-Stand aus championship.json plus Live-Hochrechnung (champ.js). Manuell ueber /regie.
class Championship : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, title, QString())
    KERS_PROP(bool, live, false)
    /// Zeigt gerade die Team-WM (ab 0.3.1, Einstellung champ_view).
    KERS_PROP(bool, teamView, false)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
public:
    static constexpr double POLL_MS = 3000;

    Championship(DataSource* source, QObject* parent = nullptr);
    QVariantList rows() const { return m_rows; }
    void setSource(DataSource* source) { m_source = source; }
    /// Feste Tabelle statt Serverabfrage - fuer den Demo-Modus.
    void setDemoData(const QJsonObject& data) { m_demo = data; }
    void clearDemoData() { m_demo.reset(); }
    /// view: "drivers" | "teams" | "wechsel" (alle 8 s umschalten)
    void update(bool wanted, double nowMs, const QString& view = QStringLiteral("drivers"));

signals:
    void rowsChanged();

private:
    void onData(const QJsonObject& data);
    void build();

    DataSource* m_source;
    QVariantList m_rows;
    QJsonObject m_data;               // letzte Antwort - fuer den Wechsel ohne neue Abfrage
    bool m_wantTeams = false;
    double m_last = 0.0;
    std::optional<QJsonObject> m_demo;
};

}  // namespace kers::hud
