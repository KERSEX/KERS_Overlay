// Battle-Boxen: Kampfgruppen erkennen und auf vier Boxen verteilen (battles.js).
//
// Die Ruhe-Regeln aus dem Original sind mitgenommen, sonst flackert das Ding:
//   * Hysterese: ein Link entsteht unter `battlethresh`, loest sich erst 0,6 s darueber
//   * Reifezeit: eine Gruppe erscheint erst, wenn sie 2 s Bestand hat
//   * Mindeststandzeit: eine sichtbare Gruppe bleibt mindestens 4 s
#include "parts.h"

#include "models.h"
#include "theme.h"

#include <QElapsedTimer>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

const QStringList& battleRoles()
{
    static const QStringList roles = {
        "slot", "driverIndex", "position", "name", "teamColor",
        "tyreIcon", "tyreStamp", "gap", "isLead", "barWidth", "barColor", "fresh",
    };
    return roles;
}

QString sig(const QList<QJsonObject>& g)
{
    return QStringLiteral("%1-%2").arg(ji(g.at(0), "index")).arg(ji(g.at(1), "index"));
}

int posOrInf(const QJsonObject& d)
{
    const int p = ji(d, "position");
    return p > 0 ? p : 999;
}

double monoSec()
{
    static QElapsedTimer t;
    if (!t.isValid())
        t.start();
    return static_cast<double>(t.nsecsElapsed()) / 1.0e9;
}

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

}  // namespace

BattleBox::BattleBox(QObject* parent)
    : QObject(parent)
    , m_rows(new SlotModel(battleRoles(), this))
{
}

Battles::Battles(Config& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
{
    for (int i = 0; i < BOX_COUNT; ++i)
        m_boxes.append(new BattleBox(this));
}

QVariantList Battles::boxes() const
{
    QVariantList out;
    for (BattleBox* b : m_boxes)
        out.append(QVariant::fromValue<QObject*>(b));
    return out;
}

int Battles::boxWunsch() const
{
    // Gewuenschte Boxenzahl aus den Settings, sicher auf 0..BOX_COUNT (0 = aus).
    return std::clamp(m_cfg.i("battleboxes"), 0, BOX_COUNT);
}

int Battles::cmpSoft(const QString& compound)
{
    // Weichere Mischung = kleinerer Wert (battles.js CMP_SOFT)
    if (compound == QLatin1String("S") || compound == QLatin1String("I"))
        return 0;
    if (compound == QLatin1String("M") || compound == QLatin1String("W"))
        return 1;
    if (compound == QLatin1String("H"))
        return 2;
    return 9;
}

// ── Ueberhol-Projektion ("Kampf in X Runden") ────────────────────────────────
void Battles::updatePrediction(const QList<QJsonObject>& drivers, bool active)
{
    if (!active) {
        m_pred.reset();
        return;
    }
    const double now = monoSec();
    std::optional<Pred> best;
    for (int i = 0; i < drivers.size(); ++i) {
        const QJsonObject& d = drivers.at(i);
        if (i == 0 || jb(d, "in_pit") || jb(d, "dnf") || jb(d, "dsq"))
            continue;
        const double gap = jd(d, "gap_to_ahead");
        const int idx = ji(d, "index");
        const auto prev = m_gapSample.constFind(idx);
        if (prev != m_gapSample.cend() && now - prev.value().first > 1.2) {
            const double rate = (prev.value().second - gap) / (now - prev.value().first);
            m_gapSample[idx] = {now, gap};
            if (rate > 0.01 && gap > 0.4 && gap < 6) {
                const double lastLap = jd(d, "last_lap");
                const double lapRef = lastLap > 30 ? lastLap : 90;
                const double laps = gap / (rate * lapRef);
                if (laps >= 1 && laps <= 9 && (!best || laps < best->laps))
                    best = Pred{static_cast<int>(std::lround(laps)), ji(drivers.at(i - 1), "index"), idx};
            }
        } else if (prev == m_gapSample.cend()) {
            m_gapSample[idx] = {now, gap};
        }
    }
    m_pred = best;
}

// ── Gruppen finden ───────────────────────────────────────────────────────────
QList<Battles::Group> Battles::findGroups(const QList<QJsonObject>& drivers)
{
    QList<Group> groups;
    Group cur;
    const double enter = m_cfg.d("battlethresh") > 0 ? m_cfg.d("battlethresh") : 1.5;
    const double exitG = enter + EXIT_MARGIN;

    auto flush = [&]() {
        if (cur.size() >= 2)
            groups.append(cur);
    };

    for (const QJsonObject& d : drivers) {
        const int idx = ji(d, "index");
        if (jb(d, "dnf") || jb(d, "dsq") || jb(d, "in_pit")) {
            m_link[idx] = false;
            flush();
            cur.clear();
            continue;
        }
        if (cur.isEmpty()) {
            cur.append(d);
            continue;
        }
        const double gap = jd(d, "gap_to_ahead");
        const bool linked = gap > 0 && (gap < enter || (m_link.value(idx, false) && gap < exitG));
        m_link[idx] = linked;
        if (linked) {
            cur.append(d);
        } else {
            flush();
            cur.clear();
            cur.append(d);
        }
    }
    flush();
    return groups;
}

void Battles::update(const QList<QJsonObject>& drivers, const QJsonObject& data,
                     const QJsonObject& session, bool active)
{
    ++m_stamp;
    const bool connected = jb(data, "connected");
    updatePrediction(drivers, m_cfg.b("pred") && connected && !jb(session, "is_quali"));

    const double now = wallMs();
    QList<Group> groups = active ? findGroups(drivers) : QList<Group>();
    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group& a, const Group& b) { return posOrInf(a.at(0)) < posOrInf(b.at(0)); });
    if (groups.size() > BOX_COUNT + 2)
        groups = groups.mid(0, BOX_COUNT + 2);
    for (Group& g : groups)
        if (g.size() > MAX_ROWS)
            g = g.mid(0, MAX_ROWS);

    // Reifezeit: die Gruppe muss 2 s bestehen, bevor sie erscheint.
    QSet<QString> seen;
    QList<Group> ready;
    for (const Group& g : groups) {
        const QString s = sig(g);
        seen.insert(s);
        if (!m_groupSeen.contains(s))
            m_groupSeen.insert(s, now);
        if (now - m_groupSeen.value(s) >= GROUP_READY_MS)
            ready.append(g);
    }
    for (auto it = m_groupSeen.begin(); it != m_groupSeen.end();) {
        if (!seen.contains(it.key()))
            it = m_groupSeen.erase(it);
        else
            ++it;
    }

    // ── Reihenfolge: der hoechste Positionskampf steht links ──────────────────
    // `ready` ist nach der Position des Fuehrenden sortiert; die Boxen werden der
    // Reihe nach bedient: P1-P3 links, P21-P22 rechts. Die Mindeststandzeit
    // haengt an der GRUPPE statt an der Box: verschwundene Gruppen bleiben als
    // Abschiedskopie in der Liste, bis ihre 4 s um sind, und sortieren sich
    // dabei ganz normal ein.
    QSet<QString> sichtbar;
    for (const Group& g : ready) {
        const QString s = sig(g);
        sichtbar.insert(s);
        if (!m_shownAt.contains(s))
            m_shownAt.insert(s, now);
        m_letzteGruppe[s] = g;
        m_abschied.remove(s);
    }

    for (auto it = m_shownAt.begin(); it != m_shownAt.end();) {
        if (sichtbar.contains(it.key())) {
            ++it;
            continue;
        }
        // Nicht mehr aktuell: noch aufheben, solange die 4 s nicht um sind.
        if (connected && now - it.value() < MIN_SHOW_MS) {
            if (m_letzteGruppe.contains(it.key()))
                m_abschied[it.key()] = m_letzteGruppe.value(it.key());
            ++it;
        } else {
            m_abschied.remove(it.key());
            m_letzteGruppe.remove(it.key());
            it = m_shownAt.erase(it);
        }
    }

    QList<Group> anzeige = ready;
    for (const Group& g : m_abschied)
        if (!g.isEmpty())
            anzeige.append(g);
    std::stable_sort(anzeige.begin(), anzeige.end(),
                     [](const Group& a, const Group& b) { return posOrInf(a.at(0)) < posOrInf(b.at(0)); });
    // Wie viele Boxen der Nutzer sehen will (Setting, 1-4). BOX_COUNT bleibt die
    // bauliche Obergrenze: die vier Boxobjekte existieren weiter, die
    // ueberzaehligen werden nur nicht mehr befuellt und blenden sich damit von
    // selbst aus (Battles.qml: width = visible ? 310 : 0).
    const int wunsch = boxWunsch();
    if (anzeige.size() > wunsch)
        anzeige = anzeige.mid(0, wunsch);

    for (int k = 0; k < m_boxes.size(); ++k) {
        BattleBox* box = m_boxes.at(k);
        if (k < anzeige.size()) {
            if (!box->isVisible()) {
                box->m_born = now;
                box->set_isVisible(true);
            }
            renderBox(box, anzeige.at(k));
        } else if (box->isVisible()) {
            box->set_isVisible(false);
            box->m_members.clear();
            box->model()->clear();
        }
    }
}

void Battles::renderBox(BattleBox* box, const Group& group)
{
    const QJsonObject& front = group.first();
    const QJsonObject& back = group.last();
    box->set_header(group.size() == 2
                        ? QStringLiteral("BATTLE FOR P%1").arg(ji(front, "position"))
                        : QStringLiteral(u"BATTLE · P%1–P%2").arg(ji(front, "position")).arg(ji(back, "position")));

    bool inGroup = false;
    if (m_pred) {
        bool hasA = false, hasB = false;
        for (const QJsonObject& d : group) {
            if (ji(d, "index") == m_pred->a)
                hasA = true;
            if (ji(d, "index") == m_pred->b)
                hasB = true;
        }
        inGroup = hasA && hasB;
    }
    box->set_subVisible(inGroup);
    if (inGroup)
        box->set_subLaps(m_pred->laps);

    QList<int> idxs;
    for (const QJsonObject& d : group)
        idxs.append(ji(d, "index"));
    std::sort(idxs.begin(), idxs.end());
    QStringList sigParts;
    for (int i : idxs)
        sigParts << QString::number(i);
    const QString memberSig = sigParts.join(QLatin1Char(','));
    const bool rebuilt = memberSig != box->m_members;
    box->m_members = memberSig;

    QList<SlotModel::Entry> entries;
    for (int i = 0; i < group.size(); ++i) {
        const QJsonObject& d = group.at(i);
        const int idx = ji(d, "index");
        const bool lead = i == 0;
        const double gap = lead ? 0.0 : jd(d, "gap_to_ahead");
        QString compound = js(d, "compound");
        if (compound.isEmpty())
            compound = QStringLiteral("?");
        const auto prev = box->m_tyreStamp.constFind(idx);
        if (prev != box->m_tyreStamp.cend() && prev.value().first != compound)
            box->m_tyreStamp[idx] = {compound, m_stamp};
        else if (prev == box->m_tyreStamp.cend())
            box->m_tyreStamp[idx] = {compound, 0};

        QVariantMap v;
        v.insert("driverIndex", idx);
        v.insert("position", ji(d, "position"));
        v.insert("name", js(d, "name"));
        v.insert("teamColor", Theme::teamColors().value(js(d, "team"), QStringLiteral("#ffffff")));
        v.insert("tyreIcon", istReifenIcon(compound) ? compound : QString());
        v.insert("tyreStamp", box->m_tyreStamp.value(idx).second);
        v.insert("gap", lead ? -1.0 : gap);
        v.insert("isLead", lead);
        v.insert("barWidth", lead ? 0.0 : barWidth(gap));
        v.insert("barColor", lead ? QStringLiteral("#ffcc00") : barColor(gap));
        v.insert("fresh", freshBadge(d, lead ? nullptr : &group.at(i - 1)));
        entries.append({idx, v});
    }
    if (rebuilt)
        box->model()->clear();
    box->model()->sync(entries);
}

double Battles::barWidth(double gap)
{
    // 0..1. Erst unter 1,0 s ueberhaupt sichtbar, voll bei 0 (battles.js barStyle).
    return std::clamp((1.0 - gap) / 1.0, 0.0, 1.0);
}

QString Battles::barColor(double gap)
{
    const double close = barWidth(gap);
    const int g = static_cast<int>(std::lround(204 - 140 * close));
    const int b = static_cast<int>(std::lround(64 * close));
    return QStringLiteral("#ff%1%2").arg(g, 2, 16, QLatin1Char('0')).arg(b, 2, 16, QLatin1Char('0'));
}

QString Battles::freshBadge(const QJsonObject& d, const QJsonObject* ahead) const
{
    // "-N Rnd" am Jaeger, wenn er deutlich frischere Reifen hat (R3.3).
    if (!m_cfg.b("fresh") || !ahead || jb(d, "in_pit") || jb(*ahead, "in_pit"))
        return QString();
    const int diff = ji(*ahead, "tyre_age") - ji(d, "tyre_age");
    const bool softer = cmpSoft(js(d, "compound")) < cmpSoft(js(*ahead, "compound"));
    if (diff >= 5 || (softer && diff >= 3))
        return QStringLiteral(u"−%1 Rnd").arg(diff);
    return QString();
}

}  // namespace kers::hud
