// Die Qt-Modelle, an denen das QML haengt - Portierung von hud/models.py.
//
// Grundgedanke: QML soll NUR darstellen. Alles, was gerechnet, verglichen oder
// gemerkt werden muss, passiert vorher - in derive.cpp und hier. Ein
// QML-Delegate, der pro Bild dreimal durch die Fahrerliste laeuft, ist bei
// 22 Zeilen und 30 Hz genau die Sorte Arbeit, die man nicht in der Szene will.
//
//   DriverModel     die Fahrerzeilen. Zeilen bleiben an ihrem Platz, die SICHTBARE
//                   Reihenfolge steht in der Rolle `slot` - siehe Kommentar dort.
//   SessionState    Kopfzeile: Titel, Runde, Flaggen, Wetter, Safety Car
//   SettingsState   die Settings vom Server als Properties (Branding, Deckkraft, ...)
//   RegieState      die manuellen Einblendungen aus /regie
//
// ⚠ Rollen- und Property-Namen sind die Schnittstelle zur unveraenderten
// QML-Szene (TowerRow.qml deklariert die Rollen als `required property`).
#pragma once

#include "derive.h"
#include "kers_prop.h"

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace kers::hud {

// Wie viele Payloads ein Fahrer fehlen darf, bevor seine Zeile verschwindet.
// 1:1 aus tower.js: ein einzelner unvollstaendiger Payload soll keine Zeile
// wegreissen (das waren die Geisterzeilen ueber P19/P20).
constexpr int MISS_TOLERANCE = 3;

/// Dateinamen in static/teams/ - identisch zu TEAM_LOGOS in static/js/core.js.
const QHash<QString, QString>& teamLogos();

class DriverModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    // ⚠ Reihenfolge = Reihenfolge in models.py ROLES. Sie ist nicht sichtbar
    // (QML greift ueber Namen zu), aber wer hier etwas einfuegt, fuegt es auch
    // in roleNames() an derselben Stelle ein.
    enum Role {
        DriverIndex = Qt::UserRole, Slot, Position, Name, Team, TeamColor, TeamLogo,
        Even, IsFocused, IsFinished, IsFastestLap, ElimZone, LapInvalid,
        ChangeDir, ChangeStamp,
        FlashKind, FlashStamp,
        GapToLeader, GapToAhead, BestLap, LastLap, LapsDown,
        Dnf, Dsq, InPit,
        Compound, TyreAge, TyreIcon, TyreStamp,
        Sectors, SectorClasses,
        QualiStatus, Drs, OvertakeActive, OvertakeAvailable,
        Penalties, PenDt, CornerWarnings, Comeback,
        DmgFl, DmgFr, DmgRw,
        RoleCount_
    };

    explicit DriverModel(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int count() const { return m_rows.size(); }

    /// Eine ganze Zeile als Objekt - praktisch fuer Fremdzugriffe aus QML.
    Q_INVOKABLE QVariantMap get(int row) const;

    /// Einen Payload einarbeiten. `drivers` ist die bereits sortierte Liste aus
    /// SharedState::derive(); der Listenplatz IST die sichtbare Zeile (`slot`).
    void update(const QList<QJsonObject>& drivers, SharedState& shared,
                const QJsonObject& session, int focusIndex, const Config& cfg);
    void clear();

signals:
    void countChanged();

private:
    struct Row {
        // Identitaet und Platz
        int driverIndex = -1;
        int slot = 0;
        int position = 0;
        QString name, team, teamColor, teamLogo;
        // Zeilenoptik
        bool even = false, isFocused = false, isFinished = false, isFastestLap = false;
        bool elimZone = false, lapInvalid = false;
        // Positionswechsel / Bestrunde-Flash
        QString changeDir = QStringLiteral("none");
        int changeStamp = 0;
        QString flashKind;
        int flashStamp = 0;
        // Zeitenspalten
        double gapToLeader = 0, gapToAhead = 0, bestLap = 0, lastLap = 0;
        int lapsDown = 0;
        bool dnf = false, dsq = false, inPit = false;
        // Reifen
        QString compound, tyreIcon;
        int tyreAge = 0, tyreStamp = 0;
        // Sektoren
        QVariantList sectors, sectorClasses;
        // Rechte Spalte
        QString qualiStatus;
        bool drs = false, overtakeActive = false, overtakeAvailable = false;
        // Anbauten
        int penalties = 0, penDt = 0, cornerWarnings = 0, comeback = 0;
        int dmgFl = 0, dmgFr = 0, dmgRw = 0;
        // Intern (keine Rollen)
        int prevSlot = 0;
        double prevBestLap = 0;
        QString prevCompound;
        int lastMove = 0;
        int pos = 0;         // Platz in m_rows - NICHT per Suche ermitteln
    };

    QList<int> fillRow(Row& row, const QJsonObject& d, int slot, bool isNew, SharedState& shared,
                       const QJsonObject& session, int focusIndex, const Config& cfg,
                       bool isQuali, const QString& fastest, int elimCut);
    void removeRow(int driverIndex);
    QVariant roleValue(const Row& r, int role) const;

    QList<Row> m_rows;             // Reihenfolge = Anlegereihenfolge
    QHash<int, int> m_posOf;       // UDP-Fahrerindex -> Platz in m_rows
    QHash<int, int> m_miss;        // UDP-Fahrerindex -> Payloads ohne diesen Fahrer
    int m_stamp = 0;               // zaehlt hoch, dient als Ausloeser fuer Animationen
};

/// Alles fuer die Kopfzeile des Towers - eine Property pro Anzeige-Element.
class SessionState : public QObject {
    Q_OBJECT
    // Verbindungen
    KERS_PROP(bool, linked, false)               // Kontakt zum Server
    KERS_PROP(bool, connected, false)            // UDP-Daten vom Spiel
    KERS_PROP(bool, holdResult, false)
    // Titelzeile
    KERS_PROP(QString, title, QString())
    KERS_PROP(QString, subtitle, QString())
    KERS_PROP(QString, flagText, QString())
    KERS_PROP(QString, flagKind, QString())      // "" | "sc" | "fin"
    // Spaltenkoepfe (Rennen vs. Quali)
    KERS_PROP(bool, isQuali, false)
    KERS_PROP(QString, boostLabel, QStringLiteral("DRS"))
    KERS_PROP(QString, headLeader, QStringLiteral("LEADER"))
    KERS_PROP(QString, headInterval, QStringLiteral("INTERVAL"))
    KERS_PROP(QString, headRight, QStringLiteral("DRS"))
    // Strecke: Name und Flaggenfarben (bewusst Farben, kein Emoji - Regional-
    // Indicator-Paare kommen in Qt als Buchstabenpaar heraus)
    KERS_PROP(QString, trackName, QString())
    KERS_PROP(QVariantList, trackColors, QVariantList())
    // Rahmen-Zustaende
    KERS_PROP(QString, scStatus, QStringLiteral("none"))   // "none" | "sc" | "vsc"
    KERS_PROP(bool, redFlag, false)
    // Quali
    KERS_PROP(double, poleTime, 0.0)
    KERS_PROP(int, elimCut, 0)
    // Wetter-Ticker: Liste von {label, emoji, rain, now}
    KERS_PROP(QVariantList, ticker, QVariantList())
public:
    explicit SessionState(QObject* parent = nullptr) : QObject(parent) {}
};

/// Die Settings vom Server, soweit das QML sie braucht.
class SettingsState : public QObject {
    Q_OBJECT
    KERS_PROP(QString, brandTitle, QString())
    KERS_PROP(QString, brandAccent, QStringLiteral("#e10600"))
    KERS_PROP(QString, headerColor, QString())
    KERS_PROP(QString, rowColor, QString())
    KERS_PROP(QString, towerLogo, QString())
    KERS_PROP(QString, towerLogoPos, QStringLiteral("left"))
    KERS_PROP(int, towerLogoH, 34)
    KERS_PROP(double, uiAlpha, 1.0)
    // Staerke der Schrift-Kontur (0..1) - macht die Schrift von uiAlpha unabhaengig lesbar.
    KERS_PROP(double, textOutline, 0.0)
    KERS_PROP(double, scale, 0.0)
    KERS_PROP(int, rows, 0)
    KERS_PROP(bool, showTower, true)
    // ⚠ Heisst showTrackName und NICHT trackName: der Name der Strecke sitzt an
    // SessionState; ein Vertipper faellt in QML nicht auf (still `undefined`).
    KERS_PROP(bool, showTrackName, true)
    KERS_PROP(bool, showTicker, true)
    KERS_PROP(int, dmgCrit, 60)
    // Platz der Trackmap (tc/tr/rc/bl/bc/br)
    KERS_PROP(QString, mapCorner, QStringLiteral("tr"))
    // Battle-Boxen nebeneinander ("row") oder gestapelt ("column")
    KERS_PROP(QString, battleDir, QStringLiteral("row"))
    // Seite der Strafen-Pillen und ob sie bei der Zielflagge weichen
    KERS_PROP(QString, penSide, QStringLiteral("left"))
    KERS_PROP(bool, penHideFinish, true)
    // Positionszahlen 1-3: "flat" oder "metal"
    KERS_PROP(QString, podiumStil, QStringLiteral("flat"))
    // Freies Layout, je Baustein {ecke, dx, dy, z, groesse}. Einzelne Felder
    // duerfen fehlen und fallen dann auf den eingebauten Wert zurueck
    // (Overlay.qml, Funktion `hat`).
    KERS_PROP(QVariantMap, layout, QVariantMap())
public:
    explicit SettingsState(QObject* parent = nullptr) : QObject(parent) {}
    void apply(const Config& cfg);
};

/// Die manuellen Einblendungen aus /regie.
class RegieState : public QObject {
    Q_OBJECT
    KERS_PROP(QString, chart, QString())    // "" | "pos" | "gap" | "lap"
    KERS_PROP(bool, champ, false)
    KERS_PROP(bool, battles, true)
    KERS_PROP(bool, hotlap, true)
public:
    explicit RegieState(QObject* parent = nullptr) : QObject(parent) {}
    void apply(const QJsonObject& regie);
};

}  // namespace kers::hud
