// SlotModel, Meldungs-Banner, Rennleitung, Undercut.
#include "parts.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSet>

namespace kers::hud {

double wallMs()
{
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch());
}

// ── SlotModel ────────────────────────────────────────────────────────────────
SlotModel::SlotModel(const QStringList& roles, QObject* parent)
    : QAbstractListModel(parent)
    , m_names(roles)
{
    for (int i = 0; i < m_names.size(); ++i) {
        m_roles.insert(Qt::UserRole + i, m_names.at(i).toLatin1());
        m_roleOf.insert(m_names.at(i), Qt::UserRole + i);
    }
}

QHash<int, QByteArray> SlotModel::roleNames() const
{
    return m_roles;
}

int SlotModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SlotModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto it = m_roles.constFind(role);
    if (it == m_roles.cend())
        return {};
    return m_rows.at(index.row()).values.value(QString::fromLatin1(it.value()));
}

void SlotModel::sync(const QList<Entry>& entries)
{
    QSet<int> seen;
    for (int slot = 0; slot < entries.size(); ++slot) {
        const int key = entries.at(slot).first;
        QVariantMap values = entries.at(slot).second;
        values.insert(QStringLiteral("slot"), slot);
        seen.insert(key);

        const auto posIt = m_posOf.constFind(key);
        if (posIt == m_posOf.cend()) {
            beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
            m_rows.append(Row{key, values});
            m_posOf.insert(key, m_rows.size() - 1);
            endInsertRows();
            emit countChanged();
            continue;
        }
        Row& row = m_rows[posIt.value()];
        QList<int> changed;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (row.values.value(it.key()) != it.value()) {
                row.values.insert(it.key(), it.value());
                const auto r = m_roleOf.constFind(it.key());
                if (r != m_roleOf.cend())
                    changed.append(r.value());
            }
        }
        if (!changed.isEmpty()) {
            const QModelIndex mi = index(posIt.value(), 0);
            emit dataChanged(mi, mi, changed);
        }
    }
    QList<int> weg;
    for (auto it = m_posOf.cbegin(); it != m_posOf.cend(); ++it)
        if (!seen.contains(it.key()))
            weg.append(it.key());
    for (int key : weg)
        removeKey(key);
}

void SlotModel::removeKey(int key)
{
    const auto it = m_posOf.constFind(key);
    if (it == m_posOf.cend())
        return;
    const int pos = it.value();
    beginRemoveRows(QModelIndex(), pos, pos);
    m_rows.removeAt(pos);
    m_posOf.remove(key);
    for (int i = pos; i < m_rows.size(); ++i)
        m_posOf[m_rows.at(i).key] = i;
    endRemoveRows();
    emit countChanged();
}

void SlotModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_posOf.clear();
    endResetModel();
    emit countChanged();
}

// ── MessageBanner ────────────────────────────────────────────────────────────
// Die Zeiten sind dieselben wie in _msgbanner.js: 2,8 s Standzeit, danach 0,45 s
// Pause, bevor die naechste kommt. Die Warteschlange ist auf 10 Eintraege
// begrenzt, damit ein Meldungsstau nicht minutenlang nachlaeuft.
MessageBanner::MessageBanner(QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &MessageBanner::hide);
    m_gap.setSingleShot(true);
    m_gap.setInterval(GAP_MS);
    connect(&m_gap, &QTimer::timeout, this, &MessageBanner::next);
}

void MessageBanner::setEnabled(bool on)
{
    m_enabled = on;
    if (!on)
        m_queue.clear();
}

void MessageBanner::push(const QString& icon, const QString& text, const QString& accent)
{
    if (!m_enabled)
        return;
    m_queue.append(Msg{icon, text, accent});
    if (m_queue.size() > MAX_QUEUE)
        m_queue.removeFirst();
    next();
}

void MessageBanner::leeren()
{
    // ⚠ Gebraucht beim Verlassen des Vorschau-Modus. Der Banner haengt an eigenen
    // Uhren statt am Datenstrom - ein leerer Stand raeumt ihn also NICHT ab.
    m_timer.stop();
    m_gap.stop();
    m_queue.clear();
    m_busy = false;
    set_isVisible(false);
}

void MessageBanner::next()
{
    if (m_busy || m_queue.isEmpty())
        return;
    m_busy = true;
    const Msg m = m_queue.takeFirst();
    set_icon(m.icon);
    set_text(m.text);
    set_accent(m.accent);
    set_isVisible(true);
    m_timer.start(SHOW_MS);
}

void MessageBanner::hide()
{
    set_isVisible(false);
    m_busy = false;
    m_gap.start();
}

// ── RaceControl ──────────────────────────────────────────────────────────────
namespace {

struct Style {
    const char16_t* icon;
    const char* color;
};

// Typ -> (Symbol, Farbe). "fl" fehlt bewusst: dafuer gibt es das grosse Banner.
const Style* styleFor(const QString& kind)
{
    static const QHash<QString, Style> styles = {
        {"penalty", {u"⚑", "#ff5a5a"}},        // ⚑
        {"tracklimit", {u"⚠", "#ffcc00"}},     // ⚠
        {"mom", {u"⚡", "#2fd9ff"}},            // ⚡
        {"retire", {u"✖", "#b0b6c0"}},         // ✖
        {"redflag", {u"⚑", "#ff2a1a"}},        // ⚑
        {"winner", {u"\U0001F3C6", "#ffd700"}},     // 🏆
        {"flag", {u"\U0001F3C1", "#ffffff"}},       // 🏁
        {"penserved", {u"✓", "#00e676"}},      // ✓
    };
    const auto it = styles.constFind(kind);
    return it == styles.cend() ? nullptr : &it.value();
}

}  // namespace

void RaceControl::update(const QJsonObject& data)
{
    const QJsonArray rc = data.value(QLatin1String("race_control")).toArray();
    if (!rc.isEmpty() && ji(rc.last().toObject(), "id") < m_lastId)
        m_lastId = 0;            // neue Session -> die Ids fangen von vorn an
    for (const QJsonValue& v : rc) {
        const QJsonObject msg = v.toObject();
        const int id = ji(msg, "id");
        if (id <= m_lastId)
            continue;
        m_lastId = id;
        const QString kind = js(msg, "type");
        if (kind == QLatin1String("fl"))
            continue;            // uebernimmt das Fastest-Lap-Banner
        const Style* st = styleFor(kind);
        const QString icon = st ? QString::fromUtf16(st->icon) : QStringLiteral(u"•");   // •
        const QString color = st ? QString::fromLatin1(st->color) : QStringLiteral("#9aa0aa");
        m_banner->push(icon, js(msg, "text"), color);
    }
}

void RaceControl::onSafetyCar(const QString& status, const QString& prev)
{
    const bool nowSc = status == QLatin1String("sc") || status == QLatin1String("vsc");
    const bool prevSc = prev == QLatin1String("sc") || prev == QLatin1String("vsc");
    if (nowSc) {
        m_banner->push(QStringLiteral(u"⚠"),
                       status == QLatin1String("sc") ? QStringLiteral("SAFETY CAR")
                                                     : QStringLiteral("VIRTUAL SAFETY CAR"),
                       QStringLiteral("#ffcc00"));
    } else if (status == QLatin1String("none") && prevSc) {
        m_banner->push(QStringLiteral(u"✓"), QStringLiteral("RENNEN FREIGEGEBEN"),
                       QStringLiteral("#00e676"));
    }
}

// ── Undercut ─────────────────────────────────────────────────────────────────
// Fahrer B boxt weniger als 2,5 s hinter A -> Versuch. Sobald A auch geboxt hat
// und wieder draussen ist, entscheidet die Position, ob er aufging.
void Undercut::leeren()
{
    // Boxen-Buchhaltung des erfundenen Rennens vergessen - sonst zaehlte ein
    // Fahrer, der in der Vorschau in der Box stand, spaeter als frisch
    // eingefahren und loeste einen erfundenen Undercut-Alarm aus.
    m_inPit.clear();
    m_attempts.clear();
}

void Undercut::update(const QList<QJsonObject>& drivers, const QJsonObject& session,
                      bool connected, bool enabled)
{
    if (!enabled || jb(session, "is_quali") || !connected) {
        m_attempts.clear();
        return;
    }
    QHash<int, const QJsonObject*> byIdx;
    for (const QJsonObject& d : drivers)
        byIdx.insert(ji(d, "index"), &d);

    for (const QJsonObject& d : drivers) {
        const int idx = ji(d, "index");
        const bool was = m_inPit.value(idx, false);
        const bool inPit = jb(d, "in_pit");
        if (inPit && !was) {
            const int pos = ji(d, "position");
            const QJsonObject* ahead = nullptr;
            for (const QJsonObject& x : drivers) {
                if (ji(x, "position") == pos - 1) {
                    ahead = &x;
                    break;
                }
            }
            const double gap = jd(d, "gap_to_ahead");
            if (ahead && !jb(*ahead, "in_pit") && gap > 0 && gap < GAP_MAX) {
                m_attempts.append(Attempt{ji(*ahead, "index"), idx, js(*ahead, "name"), js(d, "name"),
                                          1, ji(session, "current_lap")});
                m_banner->push(QStringLiteral(u"⛏"),   // ⛏
                               QStringLiteral("UNDERCUT-VERSUCH: %1 auf %2").arg(js(d, "name"), js(*ahead, "name")),
                               QStringLiteral("#ffcc00"));
            }
        }
        m_inPit[idx] = inPit;
    }

    QList<Attempt> keep;
    const int curLap = ji(session, "current_lap");
    for (Attempt u : m_attempts) {
        const QJsonObject* a = byIdx.value(u.a, nullptr);
        const QJsonObject* b = byIdx.value(u.b, nullptr);
        if (!a || !b || jb(*a, "dnf") || jb(*b, "dnf") || jb(*a, "dsq") || jb(*b, "dsq"))
            continue;
        if (curLap - u.lap0 > LAPS_MAX)
            continue;
        if (u.phase == 1 && jb(*a, "in_pit"))
            u.phase = 2;                          // der Gegner ist in der Box
        if (u.phase == 2 && !jb(*a, "in_pit")) {
            const int pa = ji(*a, "position") > 0 ? ji(*a, "position") : 99;
            const int pbb = ji(*b, "position") > 0 ? ji(*b, "position") : 99;
            if (pbb < pa)
                m_banner->push(QStringLiteral(u"✓"),
                               QStringLiteral("UNDERCUT FUNKTIONIERT: %1 vor %2").arg(u.bn, u.an),
                               QStringLiteral("#00e676"));
            else
                m_banner->push(QStringLiteral(u"✗"),   // ✗
                               QStringLiteral("UNDERCUT GESCHEITERT: %1 bleibt vor %2").arg(u.an, u.bn),
                               QStringLiteral("#ff5a5a"));
            continue;
        }
        keep.append(u);
    }
    m_attempts = keep;
}

}  // namespace kers::hud
