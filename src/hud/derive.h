// Die gemeinsame Buchhaltung des Overlays - Portierung von hud/derive.py, das
// seinerseits static/js/core.js portiert.
//
// WARUM DAS HIER LIEGT UND NICHT IN QML: diese Buchhaltung (Bestsektoren,
// Stint-Laengen, Gap-Verlauf, Safety-Car-Fenster, das eingefrorene
// Quali-Ergebnis) ist reine Rechnerei ueber Zahlenreihen. Sie laeuft EINMAL pro
// Payload, statt in QML-JavaScript in jedem der 22 Delegates erneut. QML bekommt
// fertige Werte und bleibt reine Darstellung.
//
// ⚠ ABSICHTLICH TREUE Portierung. Wo core.js/tower.js eine Eigenheit haben
// (Reihenfolge von Einfaerben und Verrechnen, Toleranzen, die Sonderbehandlung
// von S3 an der Start/Ziel-Linie), steht sie hier genauso drin - sonst sieht das
// Overlay anders aus als die Web-Fassung, die als OBS-Browserquelle daneben
// laeuft. Nicht "aufraeumen".
//
// Der Payload ist das QJsonObject aus /api/stream bzw. vom In-Process-Server;
// Fahrer sind QJsonObjects mit den snake_case-Schluesseln des Servers.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <array>
#include <functional>
#include <limits>

namespace kers::hud {

constexpr double INF = std::numeric_limits<double>::infinity();

/// Bequeme Leser fuer JSON-Felder mit denselben Rueckfaellen wie `d.get(k) or 0`.
inline double jd(const QJsonObject& o, const char* k, double def = 0.0)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble() : (v.isBool() ? (v.toBool() ? 1.0 : 0.0) : def);
}
inline int ji(const QJsonObject& o, const char* k, int def = 0)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toInt() : (v.isBool() ? (v.toBool() ? 1 : 0) : def);
}
inline bool jb(const QJsonObject& o, const char* k)
{
    const QJsonValue v = o.value(QLatin1String(k));
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString())
        return !v.toString().isEmpty();
    return false;
}
inline QString js(const QJsonObject& o, const char* k, const QString& def = QString())
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isString() ? v.toString() : def;
}

// ── Konfiguration ────────────────────────────────────────────────────────────
/// Die Settings des Servers, gemischt mit lokalen Ueberschreibungen.
///
/// Im Web-Overlay uebernehmen URL-Parameter diese Rolle (?rows=10&scale=0.8).
/// Hier ist es eine Map, die das HUD setzen kann - gleiche Regel: was lokal
/// gesetzt ist, gewinnt gegen den Server.
///
/// ⚠ apply() uebernimmt NUR Schluessel, die in defaults() stehen. Eine neue
/// Einstellung ohne Eintrag dort kaeme nie im HUD an.
class Config {
public:
    static const QVariantMap& defaults();

    explicit Config(const QVariantMap& overrides = {});

    void apply(const QJsonObject& settings);
    /// Lokal ueberschreiben. Ungueltige QVariant gibt den Wert an den Server zurueck.
    void setOverride(const QString& key, const QVariant& value);

    QVariant value(const QString& key) const;
    bool b(const char* key) const { return value(QLatin1String(key)).toBool(); }
    int i(const char* key) const { return value(QLatin1String(key)).toInt(); }
    double d(const char* key) const { return value(QLatin1String(key)).toDouble(); }
    QString s(const char* key) const { return value(QLatin1String(key)).toString(); }
    QVariantMap map(const char* key) const { return value(QLatin1String(key)).toMap(); }
    const QVariantMap& values() const { return m_values; }

private:
    QVariantMap m_override;
    QVariantMap m_values;
};

// ── Der geteilte Zustand ─────────────────────────────────────────────────────
struct Stint {
    QString compound;
    int laps = 0;
};

struct DeriveResult {
    QList<QJsonObject> drivers;    // nach Position sortiert, ggf. auf `rows` gekappt
    bool scChanged = false;        // Safety-Car-Status hat gewechselt
    QString scNow;                 // neuer Status ("none"/"sc"/"vsc"/...)
    QString scPrev;                // vorheriger Status (leer beim ersten Mal)
};

struct SectorView {
    std::array<double, 3> times{0.0, 0.0, 0.0};
    std::array<QString, 3> classes;   // "sp" (Session-Best) | "sg" (persoenlich) | "sy" | ""
};

/// Alles, was mehrere Bausteine gemeinsam lesen. Ein Objekt pro Overlay.
class SharedState {
public:
    static constexpr double TOL = 1.4;        // Pace-Toleranz in qualiStatus (core.js)
    // Ab wann ERS als "der greift an" zaehlt - an ZWEI Stellen in qualiStatus.
    static constexpr int ERS_MODE_AN = 2;     // m_ersDeployMode: 0=keins,1=mittel,2=Hotlap,3=Overtake
    static constexpr double ERS_PCT_AN = 40;
    static constexpr double SEC_EPS = 0.005;  // Toleranz beim Sektor-Vergleich (tower.js)

    explicit SharedState(Config& cfg);

    /// Monotone Uhr in ms (performance.now()). Fuer Tests austauschbar.
    void setClock(std::function<double()> clock) { m_clock = std::move(clock); }
    double nowMs() const { return m_clock(); }

    /// Nach der Quali das Ergebnis noch `holds` Sekunden stehen lassen.
    QJsonObject maybeHoldQuali(const QJsonObject& data);

    /// "box" / "out" / "inlap" / "track" (= Hotlap).
    QString qualiStatus(const QJsonObject& d) const;

    /// Hauptdurchlauf - liefert die sortierte Fahrerliste.
    DeriveResult derive(const QJsonObject& data);

    /// Welche drei Sektorzeiten stehen in der Zeile, und in welcher Farbe?
    /// ⚠ Nicht const: rechnet wie tower.js neue Bestzeiten gleich mit ein.
    SectorView sectorView(const QJsonObject& d, bool isQuali, const QString& qualiSt);

    // Von den Bausteinen gelesen
    const std::array<double, 3>& secBest() const { return m_secBest; }
    const QHash<int, std::array<double, 3>>& drvSecBest() const { return m_drvSecBest; }
    const QHash<int, std::array<double, 3>>& bestLapSecs() const { return m_bestLapSecs; }
    const QHash<int, double>& curS3() const { return m_curS3; }
    const QHash<int, QList<Stint>>& stintLen() const { return m_stintLen; }
    bool scOnboardActive() const { return m_scOnboardActive; }
    void setScOnboardActive(bool on) { m_scOnboardActive = on; }
    int scRestartUntilLap() const { return m_scRestartUntilLap; }
    void setScRestartUntilLap(int lap) { m_scRestartUntilLap = lap; }
    const QMap<int, QHash<int, double>>& gapHist() const { return m_gapHist; }
    const QMap<int, QHash<int, double>>& lapHist() const { return m_lapHist; }
    const QList<QJsonObject>& lastDrivers() const { return m_lastDrivers; }
    int lastLapCount() const { return m_lastLapCount; }

    /// Alles vergessen (Session-Wechsel, Ende der Vorschau).
    void resetSession();
    void resetAll();

    // Einzelschritte - oeffentlich, damit Tests sie direkt treffen
    void setDriverBestForTest(int idx, const std::array<double, 3>& best) { m_drvSecBest[idx] = best; }
    void mergeBestSectors(const QJsonObject& d);
    void updateRaceBest(int idx, const std::array<double, 3>& secs);
    void absorbSectors(int idx, const std::array<double, 3>& secs);
    void trackStints(const QList<QJsonObject>& drivers);

    Config& cfg() { return m_cfg; }

private:
    std::array<double, 3>& pb(int idx);
    bool greiftAn(const QJsonObject& d) const;

    Config& m_cfg;
    std::function<double()> m_clock;

    // Sektor-Buchhaltung
    std::array<double, 3> m_secBest{INF, INF, INF};
    QHash<int, std::array<double, 3>> m_drvSecBest;   // persoenliche Bestsektoren
    QHash<int, std::array<double, 3>> m_bestLapSecs;  // Sektoren der SCHNELLSTEN Runde
    QHash<int, double> m_prevLastLap;
    QHash<int, std::array<double, 2>> m_lastQualiSecs;
    QHash<int, double> m_curS3;
    bool m_haveLastSecType = false;
    int m_lastSecType = -1;

    // Stints (Lower-Third)
    QHash<int, QList<Stint>> m_stintLen;
    QHash<int, QString> m_prevComp;
    QHash<int, int> m_prevAge;

    // Safety-Car-Fenster (Onboard)
    bool m_scOnboardActive = false;
    int m_scRestartUntilLap = 0;
    bool m_haveLastSc = false;
    QString m_lastScStatus;

    // Verlaeufe (Charts)
    QMap<int, QHash<int, double>> m_gapHist;
    QMap<int, QHash<int, double>> m_lapHist;
    int m_gapLastLap = 0;

    // Eingefrorenes Quali-Ergebnis (maybeHoldQuali)
    bool m_qHaveSnap = false;
    QJsonArray m_qSnap;
    QJsonArray m_qLiveDrivers;
    QJsonObject m_qLiveSession;
    bool m_qWasQuali = false;
    double m_qSince = 0.0;

    QList<QJsonObject> m_lastDrivers;
    int m_lastLapCount = 0;
};

/// S3 der laufenden Runde, sofern die Runde schon abgeschlossen ist
/// (last_lap - S1 - S2, wie in core.js). Vor dem Rundenende 0 - auch, solange
/// das Auto noch in Sektor 3 steht (dort waere last_lap die vorige Runde).
double s3Live(const QJsonObject& d);

}  // namespace kers::hud
