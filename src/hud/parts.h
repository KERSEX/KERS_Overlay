// Der abgeleitete Zustand der einzelnen Overlay-Bausteine - Portierung von
// hud/parts.py, das seinerseits die JS-Dateien in static/parts/ portiert.
//
// Dort rechnet jede Bausteinseite in ihrem eigenen KERS.onData-Hook, hier tut es
// je eine Klasse. Die Rechnerei bleibt 1:1 dieselbe - Schwellen, Hysterese,
// Wartezeiten und Reihenfolgen sind aus dem Original uebernommen, weil sie an
// echten Rennen geradegezogen wurden.
//
//     MessageBanner    _msgbanner.js   Warteschlange fuer die Meldungen oben
//     RaceControl      racemsg.js      Strafen, Flaggen, Safety Car -> Banner
//     Undercut         undercut.js     Undercut-Erkennung -> derselbe Banner
//     Battles          battles.js      Kampfgruppen + Ueberhol-Projektion
//     Hotlaps          hotlap.js       wer faehrt gerade eine fliegende Runde
//     Onboard          onboard.js      Telemetrie des Kamera-Fahrers
//     LowerThird       lowerthird.js   Namens-Tag beim Kamerawechsel
//     PitCards         pit.js          Live-Boxenstopp-Timer
//     StartLights      lights.js       die fuenf Startlichter
//     FastestLap       flbanner.js     das lila Bestrunden-Banner
//     DangerZone       danger.js       Countdown auf dem letzten sicheren Platz
//     PitProjection    pitproj.js      "wo kaeme er nach dem Stopp raus"
//
// ⚠ Property- und Rollennamen sind die Schnittstelle zur unveraenderten QML-Szene.
#pragma once

#include "derive.h"
#include "kers_prop.h"

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace kers::hud {

// Zeilenhoehe einer Battle-Zeile - identisch zu BATTLE_ROW_H in battles.js.
constexpr int BATTLE_ROW_H = 44;

/// Wanduhr in Millisekunden - passt zu Date.now() in QML.
double wallMs();

/// Listenmodell, dessen Zeilen an ihrem Platz bleiben.
///
/// Dieselbe Idee wie DriverModel: der Schluessel ist der Fahrerindex (bzw. eine
/// Karten-Id), die SICHTBARE Reihenfolge steht in der Rolle `slot`. QML setzt
/// daraus die Y-Position und laesst eine Behavior-Animation gleiten, statt
/// Delegates umzubauen.
class SlotModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    using Entry = QPair<int, QVariantMap>;   // (key, Werte je Rollenname)

    explicit SlotModel(const QStringList& roles, QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int count() const { return m_rows.size(); }

    /// Reihenfolge der Eintraege = Platz (`slot`). Fehlende Schluessel fliegen raus.
    void sync(const QList<Entry>& entries);
    void clear();

signals:
    void countChanged();

private:
    struct Row {
        int key;
        QVariantMap values;
    };
    void removeKey(int key);

    QStringList m_names;
    QHash<int, QByteArray> m_roles;      // Rolle -> Name
    QHash<QString, int> m_roleOf;        // Name -> Rolle
    QList<Row> m_rows;
    QHash<int, int> m_posOf;             // key -> Platz in m_rows
};

// ── Meldungs-Banner ──────────────────────────────────────────────────────────
/// Ein Meldungs-Feed oben: Meldungen laufen nacheinander durch (_msgbanner.js).
///
/// ⚠ Im Gesamt-Overlay teilen sich Rennleitung UND Undercut-Alarm diesen einen
/// Banner (in templates/index.html gibt es genau ein #race-msg).
class MessageBanner : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, icon, QString())
    KERS_PROP(QString, text, QString())
    KERS_PROP(QString, accent, QStringLiteral("#00e676"))
    /// "rc" = Rennleitung/Safety Car, "strategy" = Undercut (ab 0.3.1, fuer F1 TV)
    KERS_PROP(QString, kind, QStringLiteral("rc"))
public:
    static constexpr int SHOW_MS = 2800;
    static constexpr int GAP_MS = 450;
    static constexpr int MAX_QUEUE = 10;

    explicit MessageBanner(QObject* parent = nullptr);

    /// CFG.msgs - Meldungen lassen sich in den Settings abschalten.
    void setEnabled(bool on);
    void push(const QString& icon, const QString& text, const QString& accent,
              const QString& kind = QStringLiteral("rc"));
    /// Sofort still: laufende Meldung weg, Warteschlange leer, Uhren aus.
    void leeren();

private:
    struct Msg {
        QString icon, text, accent, kind;
    };
    void next();
    void hide();

    QList<Msg> m_queue;
    bool m_busy = false;
    bool m_enabled = true;
    QTimer m_timer;
    QTimer m_gap;
};

/// Rennleitungs-Meldungen aus dem race_control-Feed (racemsg.js).
class RaceControl {
public:
    explicit RaceControl(MessageBanner* banner) : m_banner(banner) {}
    /// ⚠ Ids des erfundenen Rennens und einer echten Session fangen beide bei 1 an.
    void leeren() { m_lastId = 0; }
    void update(const QJsonObject& data);
    void onSafetyCar(const QString& status, const QString& prev);

private:
    MessageBanner* m_banner;
    int m_lastId = 0;
};

/// Undercut-Erkennung (undercut.js) - meldet in denselben Banner.
class Undercut {
public:
    static constexpr double GAP_MAX = 2.5;
    static constexpr int LAPS_MAX = 6;      // boxt der Gegner nicht, verjaehrt der Versuch

    explicit Undercut(MessageBanner* banner) : m_banner(banner) {}
    void leeren();
    void update(const QList<QJsonObject>& drivers, const QJsonObject& session, bool connected, bool enabled);

private:
    struct Attempt {
        int a, b;
        QString an, bn;
        int phase;
        int lap0;
    };
    MessageBanner* m_banner;
    QHash<int, bool> m_inPit;
    QList<Attempt> m_attempts;
};

// ── Battle-Boxen ─────────────────────────────────────────────────────────────
class Battles;

/// Eine der vier Battle-Boxen.
class BattleBox : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, header, QString())
    KERS_PROP(int, subLaps, 0)
    KERS_PROP(bool, subVisible, false)
    Q_PROPERTY(QObject* rows READ rows CONSTANT)
public:
    explicit BattleBox(QObject* parent = nullptr);
    QObject* rows() const { return m_rows; }
    SlotModel* model() const { return m_rows; }

private:
    friend class Battles;
    SlotModel* m_rows;
    QString m_members;
    double m_born = 0.0;
    QHash<int, QPair<QString, int>> m_tyreStamp;   // idx -> (compound, stamp)
};

/// Kampfgruppen erkennen und auf vier Boxen verteilen (battles.js).
class Battles : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList boxes READ boxes CONSTANT)
public:
    static constexpr int BOX_COUNT = 4;
    static constexpr int MAX_ROWS = 6;
    static constexpr double GROUP_READY_MS = 2000;
    static constexpr double MIN_SHOW_MS = 4000;
    static constexpr double EXIT_MARGIN = 0.6;

    Battles(Config& cfg, QObject* parent = nullptr);
    QVariantList boxes() const;
    void update(const QList<QJsonObject>& drivers, const QJsonObject& data,
                const QJsonObject& session, bool active);

private:
    using Group = QList<QJsonObject>;
    struct Pred {
        int laps, a, b;
    };
    int boxWunsch() const;
    void updatePrediction(const QList<QJsonObject>& drivers, bool active);
    QList<Group> findGroups(const QList<QJsonObject>& drivers);
    void renderBox(BattleBox* box, const Group& group);
    static double barWidth(double gap);
    static QString barColor(double gap);
    QString freshBadge(const QJsonObject& d, const QJsonObject* ahead) const;
    static int cmpSoft(const QString& compound);

    Config& m_cfg;
    QList<BattleBox*> m_boxes;
    QHash<int, bool> m_link;
    QHash<QString, double> m_groupSeen;
    QHash<int, QPair<double, double>> m_gapSample;   // idx -> (Zeit s, gap)
    std::optional<Pred> m_pred;
    int m_stamp = 0;
    QHash<QString, double> m_shownAt;
    QHash<QString, Group> m_letzteGruppe;
    QHash<QString, Group> m_abschied;
};

// ── Hotlap-Boxen ─────────────────────────────────────────────────────────────
/// Wer faehrt gerade eine fliegende Runde (hotlap.js).
class Hotlaps : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* boxes READ boxes CONSTANT)
public:
    static constexpr int MAX_BOXES = 4;
    static constexpr double FINISH_HOLD_MS = 5000;   // so lange steht die fertige Runde

    Hotlaps(SharedState& shared, Config& cfg, QObject* parent = nullptr);
    QObject* boxes() const { return m_rows; }
    /// `elimCut` = letzter Platz, der weiterkommt (Q1 16, Q2 10, sonst 0).
    void update(const QList<QJsonObject>& drivers, int focusIndex, int elimCut, bool active);
    /// Uhr des Nachlaufs (Wanduhr in ms) - fuer Tests austauschbar.
    void setClock(std::function<double()> uhr) { m_uhr = std::move(uhr); }

private:
    /// Wogegen gefahren wird: Bestrunde eines anderen Fahrers.
    struct Ziel {
        double runde = 0.0;                   // dessen Bestzeit
        QString name;
        std::array<double, 3> sek{0, 0, 0};   // Sektoren genau dieser Runde ...
        bool sekOk = false;                   // ... falls bekannt (sonst nur die Rundenzeit)
    };
    struct Fertig {
        QJsonObject fahrer;        // Stand vor der Linie, last_lap = gefahrene Zeit
        double bis = 0.0;          // wallMs, bis dahin steht die Karte
        int platz = MAX_BOXES;     // dort stand sie - dort bleibt sie
        double alteLast = 0.0;     // last_lap VOR der Linie
        bool zeitFest = false;     // last_lap war schon nachgezogen
        Ziel ziel;                 // vor der Linie eingefroren (fuers Delta)
        double poleVorher = 0.0;
        double bestVorher = 0.0;
    };
    int boxWunsch() const;
    void nachlaufPflegen(const QList<QJsonObject>& drivers, const QList<QJsonObject>& hot);
    void nachlaufStarten(int idx, const QJsonObject& vorher, const QJsonObject& jetzt, double now);
    QList<QJsonObject> mitNachlauf(QList<QJsonObject> hot) const;
    void zieleBestimmen(const QList<QJsonObject>& drivers, int elimCut);
    Ziel zielAus(const QJsonObject& d) const;
    const Ziel& zielFuer(const QJsonObject& d) const;
    QString secClass(double v, int idx, int i) const;

    SharedState& m_shared;
    Config& m_cfg;
    SlotModel* m_rows;
    QHash<int, QPair<QString, int>> m_tyre;
    int m_stamp = 0;
    QHash<int, QJsonObject> m_laeuft;
    QHash<int, Fertig> m_fertig;
    QHash<int, int> m_letzterPlatz;
    Ziel m_pole;
    Ziel m_cut;
    std::function<double()> m_uhr = wallMs;
};

// ── Onboard ──────────────────────────────────────────────────────────────────
/// Telemetrie des Kamera-Fahrers (onboard.js).
class Onboard : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, name, QString())
    KERS_PROP(int, position, 0)
    KERS_PROP(QString, teamColor, QStringLiteral("#ffffff"))
    KERS_PROP(QString, teamLogo, QString())
    KERS_PROP(int, speed, 0)
    KERS_PROP(QString, gear, QStringLiteral("N"))
    KERS_PROP(double, throttle, 0.0)
    KERS_PROP(double, brake, 0.0)
    KERS_PROP(bool, deltaVisible, false)
    KERS_PROP(double, delta, 0.0)
    KERS_PROP(bool, hasDelta, false)
    KERS_PROP(bool, ampVisible, false)
    /// Drei Segmente: "sp"/"sg"/"sy" gefaerbt, "run" = laufender Sektor, "" = leer.
    KERS_PROP(QVariantList, amp, QVariantList({QString(), QString(), QString()}))
public:
    Onboard(SharedState& shared, Config& cfg, QObject* parent = nullptr);
    void update(const QList<QJsonObject>& drivers, int focusIndex, bool connected,
                const QJsonObject& session, bool enabled);

private:
    SharedState& m_shared;
    Config& m_cfg;
};

// ── Lower-Third ──────────────────────────────────────────────────────────────
/// Namens-Tag beim Kamerawechsel (lowerthird.js). Nur im Rennen ab Runde 3.
class LowerThird : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, name, QString())
    KERS_PROP(int, position, 0)
    KERS_PROP(QString, team, QString())
    KERS_PROP(QString, teamColor, QStringLiteral("#ffffff"))
    KERS_PROP(QString, tyreIcon, QString())
    KERS_PROP(QString, ageText, QString())
    KERS_PROP(QString, gapText, QString())
    /// [{icon, color, letter, laps}] - Reifenbild wenn vorhanden, sonst Buchstabe.
    KERS_PROP(QVariantList, stints, QVariantList())
public:
    LowerThird(SharedState& shared, Config& cfg, QObject* parent = nullptr);
    void update(const QList<QJsonObject>& drivers, int focusIndex, const QJsonObject& session,
                bool connected, bool enabled);

private:
    SharedState& m_shared;
    Config& m_cfg;
    bool m_haveFocus = false;
    int m_focus = -1;
    QTimer m_timer;
};

// ── Boxenstopp-Karten ────────────────────────────────────────────────────────
/// Live-Boxenstopp-Timer (pit.js). Hoechstens drei Karten gleichzeitig.
class PitCards : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* cards READ cards CONSTANT)
public:
    static constexpr int MAX_CARDS = 3;
    static constexpr double PIT_LOSS = 22;      // grobe Schaetzung des Zeitverlusts eines Stopps
    static constexpr int FW_CHANGE_MIN = 10;    // ab diesem Schaden gilt ein Fluegel als gewechselt
    static constexpr double HOLD_MS = 5000;     // so lange bleibt die fertige Karte stehen

    explicit PitCards(QObject* parent = nullptr);
    QObject* cards() const { return m_cards; }
    void update(const QList<QJsonObject>& drivers, const QJsonObject& session, bool connected);

private:
    struct Track {
        bool in = false;
        QString comp, lastComp;
        int card = 0;          // 0 = keine Karte
        int fw = 0;
    };
    int start(const QJsonObject& d, const QList<QJsonObject>& drivers);
    QVariantMap* card(int cardId);
    void finish(int cardId, const QJsonObject& d, const QString& oldC, const QString& newC, int entryFw);
    void drop(int cardId);
    void flush();

    SlotModel* m_cards;
    QHash<int, Track> m_track;
    int m_nextId = 1;
    QList<QVariantMap> m_order;    // Karten in Anzeigereihenfolge (mit "_expires")
};

// ── Kleine Anzeigen ──────────────────────────────────────────────────────────
/// Die fuenf Startlichter (lights.js).
class StartLights : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(int, lit, 0)
    KERS_PROP(bool, lightsOut, false)
public:
    using QObject::QObject;
    void update(const QJsonObject& sl, bool connected, bool enabled);
};

/// Das lila Bestrunden-Banner (flbanner.js).
class FastestLap : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, name, QString())
    KERS_PROP(double, lapTime, 0.0)
public:
    FastestLap(Config& cfg, QObject* parent = nullptr);
    void update(const QJsonObject& session, bool connected, bool enabled);

private:
    Config& m_cfg;
    bool m_haveSig = false;
    QString m_sig;
    QTimer m_timer;
};

/// Countdown auf dem letzten sicheren Platz (danger.js) - letzte drei Minuten von Q1/Q2.
class DangerZone : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(int, cut, 0)
    KERS_PROP(QString, name, QString())
    KERS_PROP(int, timeLeft, 0)
public:
    using QObject::QObject;
    void update(const QList<QJsonObject>& drivers, const QJsonObject& session, bool connected, bool enabled);
};

/// "Wo kaeme er nach einem Stopp raus" (pitproj.js) - nur bei Schaden ab 30 %.
class PitProjection : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, name, QString())
    KERS_PROP(int, current, 0)
    KERS_PROP(int, projected, 0)
public:
    static constexpr double PIT_LOSS = 22;
    static constexpr int DMG_MIN = 30;

    using QObject::QObject;
    void update(const QList<QJsonObject>& drivers, int focusIndex, const QJsonObject& session,
                bool connected, bool enabled);
};

}  // namespace kers::hud
