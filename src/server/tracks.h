// Streckenkonturen: Ablage (tracks.json) und Lernen aus einer Runde.
// Portierung der _track*/_learn_outline-Funktionen aus main.py.
//
// ZWEI Quellen, und die Trennung ist der Kern:
//   builtin   in der EXE eingebacken (:/static/tracks.json), nur lesbar
//   user      data\tracks.json, waechst im Betrieb - jeder kann eine fehlende
//             Strecke selbst anlernen, indem er sie einmal faehrt
// ⚠ Die SELBST GELERNTE schlaegt die mitgelieferte.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>

namespace kers::server {

struct TrackPt {
    double x = 0, z = 0;
    double frac = -1;      // Rundenanteil 0..1, -1 = unbekannt (gelernt ohne Streckenlaenge)
    int sector = 0;
};

struct TrackEntry {
    int len = 0;
    QList<TrackPt> pts;
    std::optional<int> rot;
    std::optional<bool> flip;
};

/// Die geltende Kontur fuer die Trackmap (/api/track).
struct TrackOutline {
    QList<TrackPt> pts;
    bool done = false;
    int ver = 0;
    bool fest = false;     // kam fertig aus der Ablage -> nicht mehr ueberschreiben
};

/// Enthaelt die Kontur mehr als eine Runde? Auf die erste kuerzen.
QList<TrackPt> konturAufEineRunde(const QList<TrackPt>& pts, int laenge);
/// Ist diese Runde als Streckenkontur brauchbar (>= 60 Punkte, gueltig, 85-120 % der Laenge)?
bool rundeTaugt(const QList<TrackPt>& pts, int laenge, bool ungueltig);
/// Rohdaten aus einer tracks.json in einen sauberen Satz umwandeln (streng).
QHash<int, TrackEntry> tracksPruefen(const QJsonObject& roh);

class TrackStore {
public:
    /// builtinPath = ":/static/tracks.json", userPath = data\tracks.json
    TrackStore(const QString& builtinPath, const QString& userPath);

    /// Wird gerufen, wenn eine geladene Kontur eine Ausrichtung (rot/flip) mitbringt.
    void setAusrichtungAnwenden(std::function<void(const TrackEntry&)> cb) { m_anwenden = std::move(cb); }

    const TrackEntry* eintrag(int trackId) const;      // eigen vor mitgeliefert
    bool istEigen(int trackId) const { return m_user.contains(trackId); }
    const QHash<int, TrackEntry>& user() const { return m_user; }
    const QHash<int, TrackEntry>& builtin() const { return m_builtin; }

    /// Bekannte Kontur in `outline` setzen. True, wenn eine passte (Laenge geprueft).
    bool laden(int trackId, int trackLength, TrackOutline& outline);
    /// Frisch gelernte Kontur behalten.
    void sichern(int trackId, int trackLength, const TrackOutline& outline);
    /// Drehung/Spiegelung bei der laufenden Strecke hinterlegen (nur bekannte).
    void ausrichtungMerken(int trackId, int rot, bool flip);
    /// Eigene Kontur loeschen. True, wenn es eine gab.
    bool loeschen(int trackId);
    void speichern();

private:
    static QHash<int, TrackEntry> lesen(const QString& pfad);

    QString m_builtinPath, m_userPath;
    QHash<int, TrackEntry> m_builtin;
    QHash<int, TrackEntry> m_user;
    std::function<void(const TrackEntry&)> m_anwenden;
};

/// Was der Lerner von einem Fahrer wissen muss.
struct LearnerCar {
    int index = -1;
    int position = 0;
    bool inPit = false, dnf = false, dsq = false, lapInvalid = false;
    int lapNum = 0;
    double lapDistance = 0.0;
    int sector = 0;
};

/// Streckenkontur aus einer Runde lernen - und mit jeder weiteren verbessern.
///
/// Startet, sobald das Referenzauto echt auf der Strecke faehrt (nicht in der
/// Box), alle ~5 m ein Punkt, Schluss nach genau einer Runde. Es wird NICHT nach
/// der ersten Runde aufgehoert: JEDE brauchbare Runde ersetzt die vorige.
/// Aufgezeichnet wird in einem eigenen Puffer, nicht in der angezeigten Karte.
class TrackLearner {
public:
    TrackLearner(TrackStore& store, TrackOutline& outline) : m_store(store), m_outline(outline) {}

    /// Ein Motion-Takt: Positionen aller Autos + Fahrerdaten.
    void learn(const QHash<int, QPointF>& carPos, const QList<LearnerCar>& cars, int trackLength, int trackId);
    /// Auf eine andere Strecke gewechselt: Kontur neu bestimmen. True, wenn bekannt.
    bool streckeGewechselt(int trackId, int trackLength);
    /// Kontur verwerfen und neu lernen (/api/track/relearn).
    void neuLernen();
    /// Neue Session: Karte leeren (nicht die alte zeigen), Lernen startet neu.
    void sessionNeu();

    // Fuer /api/track
    std::optional<int> refIdx() const { return m_ref.idx >= 0 ? std::optional<int>(m_ref.idx) : std::nullopt; }
    std::optional<double> startProg() const { return m_ref.startProg; }
    double dist() const { return m_ref.dist; }

private:
    struct Ref {
        int idx = -1;
        QPointF startPt, last;
        double dist = 0.0;
        std::optional<double> startProg;
        bool invalid = false;
        bool hasStart = false;
    };
    void refReset();

    TrackStore& m_store;
    TrackOutline& m_outline;
    Ref m_ref;
    QList<TrackPt> m_buf;
};

}  // namespace kers::server
