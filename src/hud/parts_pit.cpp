// Boxenstopp-Karten: Live-Boxenstopp-Timer (pit.js). Hoechstens drei Karten.
//
// ⚠ Im Gesamt-Overlay steckt die Boxen-Erkennung mitten in der Tower-Schleife
// (test.html). Hier laeuft sie wie in pit.js als eigene Schleife.
#include "parts.h"

#include "models.h"
#include "theme.h"

#include <algorithm>

namespace kers::hud {

namespace {

const QStringList& pitRoles()
{
    static const QStringList roles = {
        "slot", "cardId", "name", "teamColor", "teamLogo", "live", "t0", "finalTime",
        "oldTyre", "newTyre", "showArrow", "newWing", "exitPos",
    };
    return roles;
}

bool istReifenIcon(const QString& c)
{
    return c == QLatin1String("S") || c == QLatin1String("M") || c == QLatin1String("H")
        || c == QLatin1String("I") || c == QLatin1String("W");
}

}  // namespace

PitCards::PitCards(QObject* parent)
    : QObject(parent)
    , m_cards(new SlotModel(pitRoles(), this))
{
}

void PitCards::update(const QList<QJsonObject>& drivers, const QJsonObject& session, bool connected)
{
    if (!connected)
        return;
    for (const QJsonObject& d : drivers) {
        const int idx = ji(d, "index");
        auto it = m_track.find(idx);
        if (it == m_track.end()) {
            Track t;
            t.comp = js(d, "compound");
            t.lastComp = t.comp;
            it = m_track.insert(idx, t);
        }
        Track& st = it.value();
        const bool inPit = jb(d, "in_pit");
        // Den Compound AUSSERHALB der Box mitschreiben - das ist der wahre "alte"
        // Reifen. Manche Quellen melden den neuen schon bei der Einfahrt.
        if (!inPit)
            st.lastComp = js(d, "compound");

        if (inPit && !st.in) {
            st.in = true;
            st.comp = st.lastComp;
            st.fw = std::max(ji(d, "dmg_fl"), ji(d, "dmg_fr"));
            // Nur im Rennen ab Runde 2 - der Startaufstellungs-Reifen zaehlt nicht.
            if (!jb(session, "is_quali") && ji(session, "current_lap") >= 2 && !jb(d, "dnf") && !jb(d, "dsq"))
                st.card = start(d, drivers);
            else
                st.card = 0;
        } else if (!inPit && st.in) {
            st.in = false;
            if (st.card) {
                finish(st.card, d, st.comp, js(d, "compound"), st.fw);
                st.card = 0;
            }
        }

        if ((jb(d, "dnf") || jb(d, "dsq")) && st.card) {
            drop(st.card);
            st.card = 0;
        }
    }
    flush();
}

int PitCards::start(const QJsonObject& d, const QList<QJsonObject>& drivers)
{
    const int cardId = m_nextId++;
    // Ungefaehre Auskommen-Position: Gap plus Pit-Verlust ins Feld einsortieren.
    const double projGap = jd(d, "gap_to_leader") + PIT_LOSS;
    int projPos = 1;
    const int idx = ji(d, "index");
    for (const QJsonObject& o : drivers)
        if (ji(o, "index") != idx && !jb(o, "dnf") && !jb(o, "dsq") && jd(o, "gap_to_leader") < projGap)
            ++projPos;
    const QString compound = js(d, "compound");
    QVariantMap c;
    c.insert("cardId", cardId);
    c.insert("name", js(d, "name"));
    c.insert("teamColor", Theme::teamColors().value(js(d, "team"), QStringLiteral("#ffffff")));
    c.insert("teamLogo", teamLogos().value(js(d, "team")));   // ab 0.3.1, fuer F1 TV
    c.insert("live", true);
    c.insert("t0", wallMs());
    c.insert("finalTime", 0.0);
    c.insert("oldTyre", QString());
    c.insert("newTyre", istReifenIcon(compound) ? compound : QString());
    c.insert("showArrow", false);
    c.insert("newWing", false);
    c.insert("exitPos", projPos);
    c.insert("_expires", 0.0);
    m_order.append(c);
    while (m_order.size() > MAX_CARDS)
        m_order.removeFirst();
    return cardId;
}

QVariantMap* PitCards::card(int cardId)
{
    for (QVariantMap& c : m_order)
        if (c.value("cardId").toInt() == cardId)
            return &c;
    return nullptr;
}

void PitCards::finish(int cardId, const QJsonObject& d, const QString& oldC, const QString& newC, int entryFw)
{
    QVariantMap* c = card(cardId);
    if (!c)
        return;
    const bool swapped = istReifenIcon(oldC) && istReifenIcon(newC) && oldC != newC;
    const int curFw = std::max(ji(d, "dmg_fl"), ji(d, "dmg_fr"));
    const QString neu = newC.isEmpty() ? oldC : newC;
    c->insert("live", false);
    c->insert("finalTime", jd(d, "pit_time"));
    c->insert("oldTyre", swapped ? oldC : QString());
    c->insert("newTyre", istReifenIcon(neu) ? neu : QString());
    c->insert("showArrow", swapped);
    // Neuer Fluegel: war bei der Einfahrt beschaedigt, ist bei der Ausfahrt
    // (fast) heil - das Spiel setzt den Schaden dann auf ~0.
    c->insert("newWing", entryFw >= FW_CHANGE_MIN && curFw <= entryFw * 0.5);
    c->insert("_expires", wallMs() + HOLD_MS);
}

void PitCards::drop(int cardId)
{
    m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
                                 [cardId](const QVariantMap& c) { return c.value("cardId").toInt() == cardId; }),
                  m_order.end());
}

void PitCards::flush()
{
    const double now = wallMs();
    m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
                                 [now](const QVariantMap& c) {
                                     const double exp = c.value("_expires").toDouble();
                                     return exp != 0.0 && now >= exp;
                                 }),
                  m_order.end());
    QList<SlotModel::Entry> entries;
    for (const QVariantMap& c : m_order) {
        QVariantMap v = c;
        v.remove("_expires");
        entries.append({c.value("cardId").toInt(), v});
    }
    m_cards->sync(entries);
}

}  // namespace kers::hud
