// Feste Tabellen des Servers - Portierung der Modul-Konstanten aus main.py.
//
// F1 26 / UDP-Format "2026". Aeltere Formate (2025, 2024) haben andere Offsets
// (Driver/Team-Id uint8 statt uint16, Name-Offset 7 statt 10, andere Strides)
// und werden nicht gelesen.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace kers::server {

constexpr int UDP_PORT_DEFAULT = 20777;
constexpr int HTTP_PORT_DEFAULT = 5100;
constexpr int EXPECTED_FORMAT = 2026;
constexpr int MAX_CARS = 24;                 // F1 26: bis zu 24 Autos in allen Arrays
constexpr int HEADER_SIZE = 29;              // '<HBBBBBQfIIBB'

// Strides je Paket (Bytes pro Auto)
constexpr int PARTICIPANT_SIZE = 60;         // Driver/Network/Team-Id jetzt uint16
constexpr int LAP_DATA_SIZE = 57;
constexpr int CAR_STATUS_SIZE = 59;          // m_ersHarvestLimitPerLap neu
constexpr int CAR_TELEM_SIZE = 59;           // m_engineTemperature jetzt uint8
constexpr int CAR_TELEM2_SIZE = 10;          // 2026 neu: Overtake Mode
constexpr int MOTION_SIZE = 54;
constexpr int FINAL_CLASS_SIZE = 46;
constexpr int DAMAGE_SIZE = 46;
constexpr int LAP_HISTORY_SIZE = 14;
constexpr double ERS_MAX = 4000000.0;

// Rennleitung
constexpr int RC_KEEP = 30;                  // wie viele Meldungen vorgehalten werden
// Eine Stop-&-Go ist im Spiel immer dieselbe Strafe. Aus dem Paket laesst sich die
// Dauer NICHT ablesen ("time" im PENA-Ereignis ist gewonnene bzw. gestandene Zeit).
constexpr int STOPGO_SEK = 10;

// Verbunden = juenger als 3 s
constexpr double CONNECTED_S = 3.0;

// Strecken-Ablage
constexpr int TRACKS_FMT = 1;
constexpr int TRACK_PTS_MAX = 4000;          // wie das Sicherheitsnetz beim Lernen
constexpr int TRACK_LEN_TOL = 30;            // m Abweichung, ab der es eine andere Streckenfuehrung ist

// Presets / Layouts
constexpr int PRESETS_MAX = 12;
constexpr int PRESET_NAME_MAX = 30;
constexpr int LAYOUTS_MAX = 12;
constexpr int LAYOUT_NAME_MAX = 30;
constexpr int CAM_MAX = 22;

struct TrackInfo {
    QString name;
    QString land;
    QStringList colors;    // Flaggenfarben statt Emoji (Regional-Indicator-Paare rendern in Qt als "IT")
};

/// (Name, Land, Farben) zu einer Strecken-ID - auch fuer unbekannte ("Strecke #N").
TrackInfo trackInfo(int trackId);

QString teamName(int teamId);                // "" wenn unbekannt
QString compoundLetter(int visual);          // "S"/"M"/"H"/"I"/"W"/"E", sonst "?"
QString weatherName(int w);                  // "Klar ☀️" ...
QString weatherEmoji(int w);
QString sessionTypeName(int t);              // "Q1", "Rennen", ...
bool isQualiType(int t);                     // 5..9
QString formulaName(int f);                  // 13 = "F1 26"
QString safetyCarStatus(int s);              // "none"/"sc"/"vsc"/"formation"
QString penaltyLabel(int type);              // "Zeitstrafe" ...

// Freies Layout: Ankerpunkte und Bausteine (Reihenfolge = Reihenfolge in /settings)
extern const QStringList LAYOUT_ECKEN;
struct LayoutTeil {
    const char* key;
    const char* label;
};
extern const LayoutTeil LAYOUT_TEILE[13];
bool isLayoutTeil(const QString& key);

// Trackmap darf nicht nach tl/lc - dort steht der Tower.
extern const QStringList MAP_CORNERS;

int championshipPoints(int position);        // 25/18/15/... fuer P1-P10, sonst 0

}  // namespace kers::server
