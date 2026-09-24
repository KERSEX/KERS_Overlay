#include "models.h"

#include "theme.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace kers::hud {

const QHash<QString, QString>& teamLogos()
{
    static const QHash<QString, QString> logos = {
        {"Red Bull", "Red_Bull_Half"}, {"Ferrari", "Ferrari"}, {"McLaren", "McLaren"},
        {"Mercedes", "Mercedes"}, {"Aston Martin", "Aston_Martin"}, {"Haas", "Haas"},
        {"RB", "Racing_Bull"}, {"Alpine", "Alpine"}, {"Williams", "Williams"},
        {"Sauber", "Audi"}, {"Audi", "Audi"}, {"Cadillac", "Cadillac"},
    };
    return logos;
}

namespace {

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

}  // namespace

// ── DriverModel ──────────────────────────────────────────────────────────────
//
// ⚠ WARUM DIE ZEILEN NICHT UMSORTIERT WERDEN
// Naheliegend waere, das Modell nach Position zu sortieren und Qt die Zeilen
// verschieben zu lassen. Dann baut QML die Delegates aber um, und genau die
// Bewegung, die man sehen WILL (P5 gleitet an P4 vorbei), findet nicht statt -
// stattdessen springt der Inhalt zwischen zwei stehenden Zeilen um.
//
// Deshalb dasselbe Verfahren wie im Web-Overlay: jede Zeile gehoert dauerhaft zu
// EINEM Fahrer (Schluessel ist sein UDP-Index), und die sichtbare Position steht
// in der Rolle `slot`. Das QML setzt `y: slot * ROW_HEIGHT` und laesst eine
// Behavior-Animation den Rest machen. Zeilen kommen nur beim Ein- und Aussteigen
// dazu oder weg.

DriverModel::DriverModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

QHash<int, QByteArray> DriverModel::roleNames() const
{
    static const QHash<int, QByteArray> names = {
        {DriverIndex, "driverIndex"}, {Slot, "slot"}, {Position, "position"}, {Name, "name"},
        {Team, "team"}, {TeamColor, "teamColor"}, {TeamLogo, "teamLogo"},
        {Even, "even"}, {IsFocused, "isFocused"}, {IsFinished, "isFinished"},
        {IsFastestLap, "isFastestLap"}, {ElimZone, "elimZone"}, {LapInvalid, "lapInvalid"},
        {ChangeDir, "changeDir"}, {ChangeStamp, "changeStamp"},
        {FlashKind, "flashKind"}, {FlashStamp, "flashStamp"},
        {GapToLeader, "gapToLeader"}, {GapToAhead, "gapToAhead"}, {BestLap, "bestLap"},
        {LastLap, "lastLap"}, {LapsDown, "lapsDown"},
        {Dnf, "dnf"}, {Dsq, "dsq"}, {InPit, "inPit"},
        {Compound, "compound"}, {TyreAge, "tyreAge"}, {TyreIcon, "tyreIcon"}, {TyreStamp, "tyreStamp"},
        {Sectors, "sectors"}, {SectorClasses, "sectorClasses"},
        {QualiStatus, "qualiStatus"}, {Drs, "drs"}, {OvertakeActive, "overtakeActive"},
        {OvertakeAvailable, "overtakeAvailable"},
        {Penalties, "penalties"}, {PenDt, "penDt"}, {CornerWarnings, "cornerWarnings"},
        {Comeback, "comeback"},
        {DmgFl, "dmgFl"}, {DmgFr, "dmgFr"}, {DmgRw, "dmgRw"},
    };
    return names;
}

int DriverModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant DriverModel::roleValue(const Row& r, int role) const
{
    switch (role) {
    case DriverIndex: return r.driverIndex;
    case Slot: return r.slot;
    case Position: return r.position;
    case Name: return r.name;
    case Team: return r.team;
    case TeamColor: return r.teamColor;
    case TeamLogo: return r.teamLogo;
    case Even: return r.even;
    case IsFocused: return r.isFocused;
    case IsFinished: return r.isFinished;
    case IsFastestLap: return r.isFastestLap;
    case ElimZone: return r.elimZone;
    case LapInvalid: return r.lapInvalid;
    case ChangeDir: return r.changeDir;
    case ChangeStamp: return r.changeStamp;
    case FlashKind: return r.flashKind;
    case FlashStamp: return r.flashStamp;
    case GapToLeader: return r.gapToLeader;
    case GapToAhead: return r.gapToAhead;
    case BestLap: return r.bestLap;
    case LastLap: return r.lastLap;
    case LapsDown: return r.lapsDown;
    case Dnf: return r.dnf;
    case Dsq: return r.dsq;
    case InPit: return r.inPit;
    case Compound: return r.compound;
    case TyreAge: return r.tyreAge;
    case TyreIcon: return r.tyreIcon;
    case TyreStamp: return r.tyreStamp;
    case Sectors: return r.sectors;
    case SectorClasses: return r.sectorClasses;
    case QualiStatus: return r.qualiStatus;
    case Drs: return r.drs;
    case OvertakeActive: return r.overtakeActive;
    case OvertakeAvailable: return r.overtakeAvailable;
    case Penalties: return r.penalties;
    case PenDt: return r.penDt;
    case CornerWarnings: return r.cornerWarnings;
    case Comeback: return r.comeback;
    case DmgFl: return r.dmgFl;
    case DmgFr: return r.dmgFr;
    case DmgRw: return r.dmgRw;
    default: return {};
    }
}

QVariant DriverModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    return roleValue(m_rows.at(index.row()), role);
}

QVariantMap DriverModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const Row& r = m_rows.at(row);
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), roleValue(r, it.key()));
    return out;
}

void DriverModel::update(const QList<QJsonObject>& drivers, SharedState& shared,
                         const QJsonObject& session, int focusIndex, const Config& cfg)
{
    ++m_stamp;
    const bool isQuali = jb(session, "is_quali");
    const QString fastest = js(session, "fastest_lap_driver");

    // Quali: Elimination-Schnitt je Segment (tower.js)
    int elimCut = 0;
    if (isQuali) {
        const int type = ji(session, "type");
        elimCut = type == 5 ? 16 : (type == 6 ? 10 : 0);
    }

    QSet<int> seen;
    for (int slot = 0; slot < drivers.size(); ++slot) {
        const QJsonObject& d = drivers.at(slot);
        const int idx = ji(d, "index");
        seen.insert(idx);
        m_miss.remove(idx);

        const auto posIt = m_posOf.constFind(idx);
        const bool isNew = posIt == m_posOf.cend();
        if (isNew) {
            Row row;
            row.driverIndex = idx;
            row.slot = slot;
            row.prevSlot = slot;
            row.pos = m_rows.size();
            // Erst befuellen, dann einfuegen: beginInsertRows laesst QML den
            // Delegate sofort bauen, und der soll die richtigen Werte sehen.
            fillRow(row, d, slot, true, shared, session, focusIndex, cfg, isQuali, fastest, elimCut);
            beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
            m_rows.append(row);
            m_posOf.insert(idx, row.pos);
            endInsertRows();
            emit countChanged();
        } else {
            Row& row = m_rows[posIt.value()];
            const QList<int> changed = fillRow(row, d, slot, false, shared, session, focusIndex, cfg,
                                               isQuali, fastest, elimCut);
            if (!changed.isEmpty()) {
                // Nur die Rollen melden, die sich wirklich geaendert haben. Meldet
                // man pauschal alle, rechnet QML pro Payload saemtliche Bindings
                // aller Zeilen neu - bei 22 Zeilen und 30 Hz genau die Arbeit,
                // die dem Spiel daneben fehlt.
                const QModelIndex mi = index(row.pos, 0);
                emit dataChanged(mi, mi, changed);
            }
        }
    }

    // Fahrer, die im Payload fehlen: erst ein paar Umlaeufe tolerieren, dann raus.
    QList<int> weg;
    for (auto it = m_posOf.cbegin(); it != m_posOf.cend(); ++it) {
        if (seen.contains(it.key()))
            continue;
        const int n = m_miss.value(it.key(), 0) + 1;
        m_miss[it.key()] = n;
        if (n >= MISS_TOLERANCE)
            weg.append(it.key());
    }
    for (int idx : weg)
        removeRow(idx);
}

void DriverModel::removeRow(int driverIndex)
{
    m_miss.remove(driverIndex);
    const auto it = m_posOf.constFind(driverIndex);
    if (it == m_posOf.cend())
        return;
    const int pos = it.value();
    beginRemoveRows(QModelIndex(), pos, pos);
    m_rows.removeAt(pos);
    m_posOf.remove(driverIndex);
    for (int i = pos; i < m_rows.size(); ++i) {
        m_rows[i].pos = i;
        m_posOf[m_rows[i].driverIndex] = i;
    }
    endRemoveRows();
    emit countChanged();
}

QList<int> DriverModel::fillRow(Row& row, const QJsonObject& d, int slot, bool isNew,
                                SharedState& shared, const QJsonObject& session, int focusIndex,
                                const Config& cfg, bool isQuali, const QString& fastest, int elimCut)
{
    Q_UNUSED(session);
    QList<int> changed;
    const int stamp = m_stamp;

    // Hilfsmakro: Wert setzen und die Rolle merken, wenn er sich geaendert hat.
#define KERS_UPD(Role, field, value)                                            \
    do {                                                                        \
        const auto neu_ = (value);                                              \
        if (!(row.field == neu_)) {                                             \
            row.field = neu_;                                                   \
            changed.append(Role);                                               \
        }                                                                       \
    } while (0)

    // Positionswechsel -> Pfeil + Aufblitzen der Nummer. Wie in tower.js: der
    // Pfeil bleibt 5 s stehen, danach wieder neutral.
    const int prevSlot = row.prevSlot;
    if (!isNew && prevSlot != slot) {
        KERS_UPD(ChangeDir, changeDir, prevSlot > slot ? QStringLiteral("up") : QStringLiteral("down"));
        KERS_UPD(ChangeStamp, changeStamp, stamp);
        row.lastMove = stamp;
    } else if (row.lastMove && (stamp - row.lastMove) > 150) {
        // 150 Payloads ~ 5 s bei 30 Hz. Bewusst in Payloads gerechnet und nicht in
        // Sekunden: der Pfeil soll an den Datenfluss gekoppelt sein (steht der
        // Strom, steht auch er).
        KERS_UPD(ChangeDir, changeDir, QStringLiteral("none"));
        row.lastMove = 0;
    }
    row.prevSlot = slot;

    // Bestrunde-Flash: neue persoenliche Bestzeit gruen, neue Session-Bestzeit lila.
    const double bestLap = jd(d, "best_lap");
    const double prevBl = row.prevBestLap;
    const QString name = js(d, "name");
    if (cfg.b("pbflash") && !isNew && bestLap > 0 && prevBl > 0 && bestLap < prevBl - 0.0005) {
        KERS_UPD(FlashKind, flashKind,
                 (!fastest.isEmpty() && name == fastest) ? QStringLiteral("p") : QStringLiteral("g"));
        KERS_UPD(FlashStamp, flashStamp, stamp);
    }
    if (bestLap > 0)
        row.prevBestLap = bestLap;

    // Reifenwechsel -> das neue Icon dreht sich herein.
    QString compound = js(d, "compound");
    if (compound.isEmpty())
        compound = QStringLiteral("?");
    if (!row.prevCompound.isEmpty() && compound != row.prevCompound)
        KERS_UPD(TyreStamp, tyreStamp, stamp);
    row.prevCompound = compound;

    const QString qualiSt = shared.qualiStatus(d);
    const SectorView sv = shared.sectorView(d, isQuali, qualiSt);

    const QString team = js(d, "team");
    const int grid = ji(d, "grid_position");
    const bool dnf = jb(d, "dnf");
    const bool dsq = jb(d, "dsq");
    const int position = ji(d, "position");
    int comeback = 0;
    if (cfg.b("comeback") && !isQuali && grid > 0 && !dnf && !dsq)
        comeback = grid - position;
    const bool ohneSchaden = isQuali || dnf || dsq || !cfg.b("damage");

    QVariantList sectors, classes;
    for (int i = 0; i < 3; ++i) {
        sectors.append(sv.times[i]);
        classes.append(sv.classes[i]);
    }

    KERS_UPD(Slot, slot, slot);
    KERS_UPD(Position, position, position);
    KERS_UPD(Name, name, name);
    KERS_UPD(Team, team, team);
    KERS_UPD(TeamColor, teamColor, Theme::teamColors().value(team, QStringLiteral("#ffffff")));
    KERS_UPD(TeamLogo, teamLogo, teamLogos().value(team));
    KERS_UPD(Even, even, slot % 2 == 0);
    KERS_UPD(IsFocused, isFocused, focusIndex >= 0 && ji(d, "index") == focusIndex);
    KERS_UPD(IsFinished, isFinished, jb(d, "finished"));
    KERS_UPD(IsFastestLap, isFastestLap, !fastest.isEmpty() && name == fastest);
    KERS_UPD(ElimZone, elimZone, elimCut > 0 && position > elimCut);
    // Track-Limits faerben die Sektoren nur auf einem echten Hotlap rot.
    KERS_UPD(LapInvalid, lapInvalid, isQuali && qualiSt == QLatin1String("track") && jb(d, "lap_invalid"));
    KERS_UPD(GapToLeader, gapToLeader, jd(d, "gap_to_leader"));
    KERS_UPD(GapToAhead, gapToAhead, jd(d, "gap_to_ahead"));
    KERS_UPD(BestLap, bestLap, bestLap);
    KERS_UPD(LastLap, lastLap, jd(d, "last_lap"));
    KERS_UPD(LapsDown, lapsDown, ji(d, "laps_down"));
    KERS_UPD(Dnf, dnf, dnf);
    KERS_UPD(Dsq, dsq, dsq);
    KERS_UPD(InPit, inPit, jb(d, "in_pit"));
    KERS_UPD(Compound, compound, compound);
    KERS_UPD(TyreAge, tyreAge, ji(d, "tyre_age"));
    KERS_UPD(TyreIcon, tyreIcon, istReifenIcon(compound) ? compound : QString());
    KERS_UPD(Sectors, sectors, sectors);
    KERS_UPD(SectorClasses, sectorClasses, classes);
    KERS_UPD(QualiStatus, qualiStatus, qualiSt);
    KERS_UPD(Drs, drs, jb(d, "drs"));
    KERS_UPD(OvertakeActive, overtakeActive, jb(d, "overtake_active"));
    KERS_UPD(OvertakeAvailable, overtakeAvailable, jb(d, "overtake_available"));
    // Strafen gibt es nur im Rennen - in der Quali bleiben die Pillen leer.
    KERS_UPD(Penalties, penalties, isQuali ? 0 : ji(d, "penalties"));
    KERS_UPD(PenDt, penDt, isQuali ? 0 : ji(d, "pen_dt"));
    // Bei der 3. Verwarnung gibt es die Strafe und der Zaehler beginnt von vorn
    // -> Modulo 3, genau wie in tower.js.
    KERS_UPD(CornerWarnings, cornerWarnings, isQuali ? 0 : ji(d, "corner_warnings") % 3);
    KERS_UPD(Comeback, comeback, comeback);
    // Ausgeschiedene zeigen keinen Schaden mehr (sonst blinkt es ewig weiter).
    KERS_UPD(DmgFl, dmgFl, ohneSchaden ? 0 : ji(d, "dmg_fl"));
    KERS_UPD(DmgFr, dmgFr, ohneSchaden ? 0 : ji(d, "dmg_fr"));
    KERS_UPD(DmgRw, dmgRw, ohneSchaden ? 0 : ji(d, "dmg_rw"));
#undef KERS_UPD

    // Frisch eingefuegte Zeilen brauchen kein dataChanged - beginInsertRows hat
    // QML gerade erst dazu gebracht, den Delegate mit genau diesen Werten zu bauen.
    return isNew ? QList<int>() : changed;
}

void DriverModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_posOf.clear();
    m_miss.clear();
    endResetModel();
    emit countChanged();
}

// ── SettingsState ────────────────────────────────────────────────────────────
void SettingsState::apply(const Config& cfg)
{
    const double opacity = cfg.d("opacity") > 0 ? cfg.d("opacity") : 1.0;
    auto oder = [](const QString& s, const char* def) { return s.isEmpty() ? QString::fromLatin1(def) : s; };

    set_brandTitle(cfg.s("brand_title"));
    set_brandAccent(oder(cfg.s("brand_accent"), "#e10600"));
    set_headerColor(cfg.s("header_color"));
    set_rowColor(cfg.s("row_color"));
    set_towerLogo(cfg.s("tower_logo"));
    set_towerLogoPos(oder(cfg.s("tower_logo_pos"), "left"));
    set_towerLogoH(cfg.i("tower_logo_h") > 0 ? cfg.i("tower_logo_h") : 34);
    // Grenzen wie in applyBrand() in core.js: nach unten 0.2, nach oben 1.25
    // (darueber ist ohnehin alles deckend).
    set_uiAlpha(std::clamp(opacity, 0.2, 1.25));
    set_textOutline(std::clamp(cfg.d("text_outline"), 0.0, 1.0));
    set_scale(cfg.d("scale"));
    set_rows(cfg.i("rows"));
    set_showTower(cfg.b("tower"));
    set_showTrackName(cfg.b("trackname"));
    set_showTicker(cfg.b("ticker"));
    set_dmgCrit(cfg.i("dmgcrit") > 0 ? cfg.i("dmgcrit") : 60);
    set_mapCorner(oder(cfg.s("mapcorner"), "tr"));
    set_battleDir(cfg.s("battledir") == QLatin1String("column") ? QStringLiteral("column") : QStringLiteral("row"));
    set_penSide(cfg.s("penside") == QLatin1String("right") ? QStringLiteral("right") : QStringLiteral("left"));
    set_penHideFinish(cfg.b("penhidefinish"));
    set_podiumStil(cfg.s("podiumstil") == QLatin1String("metal") ? QStringLiteral("metal") : QStringLiteral("flat"));
    set_layout(cfg.map("layout"));
}

// ── RegieState ───────────────────────────────────────────────────────────────
void RegieState::apply(const QJsonObject& regie)
{
    const QString chart = js(regie, "chart");
    set_chart((chart.isEmpty() || chart == QLatin1String("none")) ? QString() : chart);
    set_champ(jb(regie, "champ"));
    // Vorgabe an: nur ein ausdrueckliches false schaltet ab.
    const QJsonValue b = regie.value(QLatin1String("battles"));
    set_battles(!(b.isBool() && !b.toBool()));
    const QJsonValue h = regie.value(QLatin1String("hotlap"));
    set_hotlap(!(h.isBool() && !h.toBool()));
}

}  // namespace kers::hud
