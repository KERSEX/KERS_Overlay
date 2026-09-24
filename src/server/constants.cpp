#include "constants.h"

namespace kers::server {

namespace {

struct TrackRow {
    int id;
    const char* name;
    const char* land;
    const char* c1;
    const char* c2;
    const char* c3;    // leer = nur zwei Farben
};

// Die seit Jahren stabile Nummerierung der F1-Reihe (0 = Melbourne bis 32 = Losail).
// 39/40 sind die Rueckwaerts-Varianten, aus KERS' gefahrenen Daten nachgemessen.
const TrackRow kTracks[] = {
    {0, "Melbourne", "Australien", "#00008B", "#FFFFFF", "#FF0000"},
    {1, "Paul Ricard", "Frankreich", "#002395", "#FFFFFF", "#ED2939"},
    {2, "Shanghai", "China", "#DE2910", "#FFDE00", ""},
    {3, "Sakhir", "Bahrain", "#CE1126", "#FFFFFF", ""},
    {4, "Barcelona", "Spanien", "#AA151B", "#F1BF00", ""},
    {5, "Monaco", "Monaco", "#CE1126", "#FFFFFF", ""},
    {6, "Montreal", "Kanada", "#FF0000", "#FFFFFF", ""},
    {7, "Silverstone", "Grossbritannien", "#012169", "#FFFFFF", "#C8102E"},
    {8, "Hockenheim", "Deutschland", "#000000", "#DD0000", "#FFCE00"},
    {9, "Hungaroring", "Ungarn", "#CD2A3E", "#FFFFFF", "#436F4D"},
    {10, "Spa", "Belgien", "#000000", "#FDDA24", "#EF3340"},
    {11, "Monza", "Italien", "#008C45", "#F4F5F0", "#CD212A"},
    {12, "Singapur", "Singapur", "#EF3340", "#FFFFFF", ""},
    {13, "Suzuka", "Japan", "#FFFFFF", "#BC002D", ""},
    {14, "Abu Dhabi", "VAE", "#00732F", "#FFFFFF", "#CE1126"},
    {15, "Austin", "USA", "#3C3B6E", "#FFFFFF", "#B22234"},
    {16, "Interlagos", "Brasilien", "#009C3B", "#FFDF00", "#002776"},
    {17, "Red Bull Ring", "Oesterreich", "#ED2939", "#FFFFFF", ""},
    {18, "Sotschi", "Russland", "#FFFFFF", "#0039A6", "#D52B1E"},
    {19, "Mexiko-Stadt", "Mexiko", "#006847", "#FFFFFF", "#CE1126"},
    {20, "Baku", "Aserbaidschan", "#00B5E2", "#EF3340", "#509E2F"},
    {21, "Sakhir (kurz)", "Bahrain", "#CE1126", "#FFFFFF", ""},
    {22, "Silverstone (kurz)", "Grossbritannien", "#012169", "#FFFFFF", "#C8102E"},
    {23, "Austin (kurz)", "USA", "#3C3B6E", "#FFFFFF", "#B22234"},
    {24, "Suzuka (kurz)", "Japan", "#FFFFFF", "#BC002D", ""},
    {25, "Hanoi", "Vietnam", "#DA251D", "#FFFF00", ""},
    {26, "Zandvoort", "Niederlande", "#AE1C28", "#FFFFFF", "#21468B"},
    {27, "Imola", "Italien", "#008C45", "#F4F5F0", "#CD212A"},
    {28, "Portimao", "Portugal", "#046A38", "#DA291C", ""},
    {29, "Dschidda", "Saudi-Arabien", "#006C35", "#FFFFFF", ""},
    {30, "Miami", "USA", "#3C3B6E", "#FFFFFF", "#B22234"},
    {31, "Las Vegas", "USA", "#3C3B6E", "#FFFFFF", "#B22234"},
    {32, "Losail", "Katar", "#8A1538", "#FFFFFF", ""},
    {39, "Silverstone rueckwaerts", "Grossbritannien", "#012169", "#FFFFFF", "#C8102E"},
    {40, "Red Bull Ring rueckwaerts", "Oesterreich", "#ED2939", "#FFFFFF", ""},
};

}  // namespace

TrackInfo trackInfo(int trackId)
{
    for (const TrackRow& r : kTracks) {
        if (r.id == trackId) {
            TrackInfo t{QString::fromLatin1(r.name), QString::fromLatin1(r.land), {}};
            t.colors << QString::fromLatin1(r.c1) << QString::fromLatin1(r.c2);
            if (r.c3[0])
                t.colors << QString::fromLatin1(r.c3);
            return t;
        }
    }
    // Nicht leer lassen: eine unbenannte Strecke soll trotzdem einen
    // wiedererkennbaren Namen haben.
    return TrackInfo{trackId >= 0 ? QStringLiteral("Strecke #%1").arg(trackId) : QStringLiteral("Unbekannte Strecke"),
                     QString(), {}};
}

QString teamName(int teamId)
{
    // Team-Ids laut F1 26 Appendix: Basis-Ids (0-9) und die eigenstaendigen
    // '26-Ids (476-486), damit es egal ist, welche das Spiel sendet.
    switch (teamId) {
    case 0: case 476: return QStringLiteral("Mercedes");
    case 1: case 477: return QStringLiteral("Ferrari");
    case 2: case 478: return QStringLiteral("Red Bull");
    case 3: case 479: return QStringLiteral("Williams");
    case 4: case 480: return QStringLiteral("Aston Martin");
    case 5: case 481: return QStringLiteral("Alpine");
    case 6: case 482: return QStringLiteral("RB");
    case 7: case 483: return QStringLiteral("Haas");
    case 8: case 484: return QStringLiteral("McLaren");
    case 9: case 485: return QStringLiteral("Audi");     // 2026: Sauber -> Audi
    case 486: return QStringLiteral("Cadillac");
    case 41: return QStringLiteral("F1 Generic");
    case 104: return QStringLiteral("Custom Team");
    default: return QString();
    }
}

QString compoundLetter(int visual)
{
    switch (visual) {
    case 0: case 8: return QStringLiteral("W");
    case 1: case 7: return QStringLiteral("I");
    case 2: return QStringLiteral("E");
    case 16: return QStringLiteral("S");
    case 17: return QStringLiteral("M");
    case 18: return QStringLiteral("H");
    default: return QStringLiteral("?");
    }
}

QString weatherName(int w)
{
    switch (w) {
    case 0: return QStringLiteral(u"Klar ☀️");
    case 1: return QStringLiteral(u"Leicht bewölkt \U0001F324️");
    case 2: return QStringLiteral(u"Bewölkt ☁️");
    case 3: return QStringLiteral(u"Leichter Regen \U0001F326️");
    case 4: return QStringLiteral(u"Starker Regen \U0001F327️");
    case 5: return QStringLiteral(u"Gewitter ⛈️");
    default: return QStringLiteral(u"Klar ☀️");
    }
}

QString weatherEmoji(int w)
{
    switch (w) {
    case 0: return QStringLiteral(u"☀️");
    case 1: return QStringLiteral(u"\U0001F324️");
    case 2: return QStringLiteral(u"☁️");
    case 3: return QStringLiteral(u"\U0001F326️");
    case 4: return QStringLiteral(u"\U0001F327️");
    case 5: return QStringLiteral(u"⛈️");
    default: return QStringLiteral(u"☀️");
    }
}

QString sessionTypeName(int t)
{
    switch (t) {
    case 1: return QStringLiteral("P1");
    case 2: return QStringLiteral("P2");
    case 3: return QStringLiteral("P3");
    case 4: return QStringLiteral("Short P");
    case 5: return QStringLiteral("Q1");
    case 6: return QStringLiteral("Q2");
    case 7: return QStringLiteral("Q3");
    case 8: return QStringLiteral("Short Q");
    case 9: return QStringLiteral("OSQ");
    case 10: return QStringLiteral("SSO1");
    case 11: return QStringLiteral("SSO2");
    case 12: return QStringLiteral("SSO3");
    case 13: return QStringLiteral("Short SSO");
    case 14: return QStringLiteral("OSSO");
    case 15: return QStringLiteral("Rennen");
    case 16: return QStringLiteral("Rennen 2");
    case 17: return QStringLiteral("Rennen 3");
    case 18: return QStringLiteral("Zeitrennen");
    default: return QStringLiteral("Unbekannt");
    }
}

bool isQualiType(int t)
{
    return t >= 5 && t <= 9;
}

QString formulaName(int f)
{
    switch (f) {
    case 0: return QStringLiteral("F1");
    case 1: return QStringLiteral("F1 Classic");
    case 2: return QStringLiteral("F2");
    case 3: return QStringLiteral("F1 Generic");
    case 4: return QStringLiteral("Beta");
    case 6: return QStringLiteral("Esports");
    case 8: return QStringLiteral("F1W");
    case 9: return QStringLiteral("F1 Elimination");
    case 13: return QStringLiteral("F1 26");
    default: return QStringLiteral("F1");
    }
}

QString safetyCarStatus(int s)
{
    switch (s) {
    case 1: return QStringLiteral("sc");
    case 2: return QStringLiteral("vsc");
    case 3: return QStringLiteral("formation");
    default: return QStringLiteral("none");
    }
}

QString penaltyLabel(int type)
{
    switch (type) {
    case 0: return QStringLiteral("Drive Through Penalty");
    case 1: return QStringLiteral("%1 Sek Strafe").arg(STOPGO_SEK);
    case 2: return QStringLiteral("Startplatzstrafe");
    case 4: return QStringLiteral("Zeitstrafe");
    case 5: return QStringLiteral("Verwarnung");
    case 6: return QStringLiteral("Disqualifiziert");
    case 9: return QStringLiteral("Reifen-Regel");
    default: return QStringLiteral("Strafe");
    }
}

const QStringList LAYOUT_ECKEN = {"tl", "tc", "tr", "lc", "cc", "rc", "bl", "bc", "br"};

const LayoutTeil LAYOUT_TEILE[13] = {
    {"tower", "Timing Tower"},
    {"trackmap", "Trackmap"},
    {"racemsg", "Meldungs-Banner"},
    {"flbanner", "Fastest-Lap-Banner"},
    {"lights", "Start-Ampel"},
    {"battles", "Battle-Boxen"},
    {"hotlaps", "Hotlap-Boxen"},
    {"lowerthird", "Lower-Third"},
    {"danger", "Gefahrenzone"},
    {"onboard", "Onboard"},
    {"pitproj", "Pit-Projektion"},
    {"pitcards", "Boxenstopp-Karten"},
    {"champ", "WM-Stand"},
};

bool isLayoutTeil(const QString& key)
{
    for (const LayoutTeil& t : LAYOUT_TEILE)
        if (key == QLatin1String(t.key))
            return true;
    return false;
}

const QStringList MAP_CORNERS = {"tc", "tr", "rc", "bl", "bc", "br"};

int championshipPoints(int position)
{
    static const int pts[] = {0, 25, 18, 15, 12, 10, 8, 6, 4, 2, 1};
    return (position >= 1 && position <= 10) ? pts[position] : 0;
}

}  // namespace kers::server
