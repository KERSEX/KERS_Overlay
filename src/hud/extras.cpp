#include "extras.h"

#include "models.h"
#include "theme.h"

#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace kers::hud {

namespace {

// Sektorfarben der Kontur - identisch zu SECTOR_COLORS in trackmap.js.
QString sectorColor(int s)
{
    switch (s) {
    case 1: return QStringLiteral("#2f7bff");
    case 2: return QStringLiteral("#e10600");
    default: return QStringLiteral("#eaeaf0");
    }
}

QVariantList pointList(const QList<QPointF>& pts)
{
    QVariantList out;
    out.reserve(pts.size());
    for (const QPointF& p : pts)
        out.append(QVariant::fromValue(p));
    return out;
}

}  // namespace

// ── Trackmap ─────────────────────────────────────────────────────────────────
Trackmap::Trackmap(Config& cfg, DataSource* source, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_source(source)
{
    m_timer.setInterval(POLL_MS);
    connect(&m_timer, &QTimer::timeout, this, &Trackmap::refresh);
}

void Trackmap::start()
{
    refresh();
    m_timer.start();
}

void Trackmap::stop()
{
    m_timer.stop();
}

void Trackmap::refresh()
{
    if (!m_source)
        return;
    m_source->get(QStringLiteral("/api/track"), [this](const QJsonObject& t) { onTrack(t); });
}

void Trackmap::loadPoints(const QJsonArray& pts, int ver)
{
    QJsonObject t;
    t["done"] = true;
    t["pts"] = pts;
    t["ver"] = ver;
    onTrack(t);
}

void Trackmap::onTrack(const QJsonObject& t)
{
    // Die Kontur wird vom Server gelernt und ist erst brauchbar, wenn `done` kommt.
    m_done = jb(t, "done");
    const QJsonArray pts = t.value(QLatin1String("pts")).toArray();
    if (!m_done || pts.size() < 20)
        return;
    const int ver = ji(t, "ver");
    if (ver == m_ver)
        return;
    m_ver = ver;

    QList<Pt> raw;
    raw.reserve(pts.size());
    for (const QJsonValue& v : pts) {
        const QJsonArray a = v.toArray();
        Pt p;
        p.n = a.size();
        if (a.size() >= 2) {
            p.x = a.at(0).toDouble();
            p.z = a.at(1).toDouble();
        }
        if (a.size() >= 3)
            p.frac = a.at(2).toDouble(-1);
        if (a.size() >= 4)
            p.sector = a.at(3).toInt();
        raw.append(p);
    }
    m_pts = smoothLoop(raw);

    double minX = std::numeric_limits<double>::infinity(), maxX = -minX;
    double minZ = minX, maxZ = -minX;
    for (const Pt& p : m_pts) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }
    const double spanX = (maxX - minX) != 0 ? (maxX - minX) : 1;
    const double spanZ = (maxZ - minZ) != 0 ? (maxZ - minZ) : 1;
    // Grobe Vorskalierung; den genauen Sitz macht danach updateFit().
    const double pad = 15, size = 70;
    const double sc = size / std::max(spanX, spanZ);   // EIN Faktor -> keine Verzerrung
    m_bounds = Bounds{minX, minZ, sc, pad + (size - spanX * sc) / 2, pad + (size - spanZ * sc) / 2};
    ensureTransform(true);
    updateFit();
    buildContour();
    m_flagSig.clear();
}

bool Trackmap::ensureTransform(bool force)
{
    const int rot = m_cfg.i("maprot");
    const bool flip = m_cfg.b("mapflip");
    const QString sig = QStringLiteral("%1|%2").arg(rot).arg(flip ? 1 : 0);
    if (sig == m_tfSig && !force)
        return false;
    m_tfSig = sig;
    const double rad = rot * M_PI / 180.0;
    m_cos = std::cos(rad);
    m_sin = std::sin(rad);
    m_flip = flip;
    return true;
}

void Trackmap::updateFit()
{
    // Die GEDREHTE Kontur formatfuellend in die Karte einpassen. Der Rand ist
    // nur noch so breit wie Linien und Punkte ihn brauchen.
    m_fitF = 1.0;
    m_fitCx = 50.0;
    m_fitCz = 50.0;
    if (m_pts.isEmpty() || !m_bounds)
        return;
    double minX = std::numeric_limits<double>::infinity(), maxX = -minX;
    double minZ = minX, maxZ = -minX;
    for (const Pt& p : m_pts) {
        const QPointF r = rotated(p.x, p.z);
        minX = std::min(minX, r.x()); maxX = std::max(maxX, r.x());
        minZ = std::min(minZ, r.y()); maxZ = std::max(maxZ, r.y());
    }
    const double span = std::max(maxX - minX, maxZ - minZ);
    if (span <= 0)
        return;
    m_fitF = FIT_TARGET / span;
    m_fitCx = (maxX + minX) / 2;
    m_fitCz = (maxZ + minZ) / 2;
}

QPointF Trackmap::rotated(double x, double z) const
{
    // Weltkoordinaten -> gedreht/gespiegelt, aber noch ohne Einpassung.
    const Bounds& b = *m_bounds;
    const double nx = b.offX + (x - b.minX) * b.sc;
    const double nz = 100 - (b.offZ + (z - b.minZ) * b.sc);     // z-Achse umdrehen
    const double dx = nx - 50, dz = nz - 50;
    double rx = dx * m_cos - dz * m_sin;
    const double rz = dx * m_sin + dz * m_cos;
    if (m_flip)
        rx = -rx;
    return QPointF(rx + 50, rz + 50);
}

std::optional<QPointF> Trackmap::norm(double x, double z) const
{
    // Weltkoordinaten -> 0..100, gedreht, gespiegelt und eingepasst (normPoint).
    if (!m_bounds)
        return std::nullopt;
    const QPointF r = rotated(x, z);
    return QPointF((r.x() - m_fitCx) * m_fitF + 50, (r.y() - m_fitCz) * m_fitF + 50);
}

void Trackmap::buildContour()
{
    if (m_pts.isEmpty() || !m_bounds)
        return;
    const bool hasMeta = m_pts.first().n >= 4 && m_cfg.b("mapflags");
    QList<QPointF> loop;
    loop.reserve(m_pts.size() + 1);
    for (const Pt& p : m_pts)
        loop.append(*norm(p.x, p.z));
    loop.append(loop.first());

    QVariantList parts;
    auto part = [](const QString& color, double width, const QList<QPointF>& pts) {
        QVariantMap m;
        m.insert("color", color);
        m.insert("width", width);
        m.insert("points", pointList(pts));
        return m;
    };
    // Unterste Schicht: eine durchgehende dunkle Linie ("Casing") als Trennstreifen.
    parts.append(part(QStringLiteral("#0b0b12"), 4.4, loop));

    if (!hasMeta) {
        parts.append(part(sectorColor(0), 2.0, loop));
    } else {
        QList<Pt> run{m_pts.first()};
        auto flush = [&](const Pt* bridge) {
            QList<Pt> seg = run;
            if (bridge)
                seg.append(*bridge);
            if (seg.size() > 1) {
                QList<QPointF> pts;
                for (const Pt& p : seg)
                    pts.append(*norm(p.x, p.z));
                parts.append(part(sectorColor(run.first().sector), 2.0, pts));
            }
        };
        for (int i = 1; i < m_pts.size(); ++i) {
            const Pt& p = m_pts.at(i);
            if (p.sector == run.first().sector) {
                run.append(p);
            } else {
                flush(&p);
                run = QList<Pt>{p};
            }
        }
        flush(&m_pts.first());           // Schleife schliessen
    }
    m_contour = parts;
    emit contourChanged();
}

void Trackmap::buildFlags(const QJsonArray& zones)
{
    const bool usable = m_cfg.b("mapflags") && !zones.isEmpty() && !m_pts.isEmpty()
        && m_pts.first().n >= 3 && m_pts.first().frac >= 0 && m_bounds;
    if (!usable) {
        if (!m_flags.isEmpty()) {
            m_flags.clear();
            emit contourChanged();
        }
        m_flagSig.clear();
        return;
    }
    QStringList fs;
    for (const QJsonValue& z : zones)
        fs << QString::number(ji(z.toObject(), "f"));
    const QString sig = fs.join(QLatin1Char(',')) + QLatin1Char('|') + m_tfSig + QLatin1Char('|') + QString::number(m_ver);
    if (sig == m_flagSig)
        return;
    m_flagSig = sig;

    auto inZone = [](double frac, double a, double b) {
        return a <= b ? (a <= frac && frac < b) : (frac >= a || frac < b);
    };

    QVariantList out;
    for (int i = 0; i < zones.size(); ++i) {
        const QJsonObject z = zones.at(i).toObject();
        if (ji(z, "f") != 3)                     // nur gelbe Flaggen
            continue;
        const double a = jd(z, "s");
        const double b = i + 1 < zones.size() ? jd(zones.at(i + 1).toObject(), "s") : jd(zones.at(0).toObject(), "s");
        QList<QPointF> run;
        for (const Pt& p : m_pts) {
            // ⚠ frac == -1 (gelernt, bevor die Streckenlaenge bekannt war) darf
            // NICHT flaggen - sonst waere die ganze Kontur gelb.
            if (p.frac >= 0 && p.frac <= 1 && inZone(p.frac, a, b)) {
                run.append(*norm(p.x, p.z));
            } else {
                if (run.size() > 1) {
                    QVariantMap m;
                    m.insert("points", pointList(run));
                    out.append(m);
                }
                run.clear();
            }
        }
        if (run.size() > 1) {
            QVariantMap m;
            m.insert("points", pointList(run));
            out.append(m);
        }
    }
    m_flags = out;
    emit contourChanged();
}

void Trackmap::update(const QList<QJsonObject>& drivers, int focusIndex, bool connected,
                      const QJsonObject& session, bool enabled, double nowMs)
{
    if (!enabled || !connected || !m_bounds || !m_done) {
        set_isVisible(false);
        return;
    }
    set_isVisible(true);
    set_size(m_cfg.i("mapsize") > 0 ? m_cfg.i("mapsize") : 400);
    if (ensureTransform()) {
        updateFit();                       // gedrehte Kontur neu einpassen
        buildContour();
        m_flagSig.clear();
    }
    buildFlags(session.value(QLatin1String("marshal_zones")).toArray());

    const double dot = m_cfg.d("dotsize") > 0 ? m_cfg.d("dotsize") : 1.0;
    const bool showNumbers = m_cfg.b("mapnumbers");
    const bool isQuali = jb(session, "is_quali");

    // Ausgeschiedene: on track aufgegeben -> Punkt bleibt kurz verblassend stehen.
    // In der Box aufgegeben -> gar kein Punkt.
    for (const QJsonObject& d : drivers) {
        const int idx = ji(d, "index");
        const QJsonArray pos = d.value(QLatin1String("pos_xz")).toArray();
        if (pos.size() >= 2)
            m_lastPos[idx] = QPointF(pos.at(0).toDouble(), pos.at(1).toDouble());
        const bool retired = jb(d, "dnf") || jb(d, "dsq");
        const bool hasLast = m_lastPos.contains(idx);
        if (retired && jb(d, "in_pit")) {
            m_ghost.remove(idx);
        } else if (retired && hasLast) {
            if (!m_ghost.contains(idx) && !m_ghostDone.contains(idx)) {
                const QPointF last = m_lastPos.value(idx);
                m_ghost.insert(idx, Ghost{nowMs, last.x(), last.y(),
                                          Theme::teamColors().value(js(d, "team"), QStringLiteral("#767680"))});
            }
        } else if (!retired) {
            m_ghost.remove(idx);
            m_ghostDone.remove(idx);
        }
    }

    QVariantList cars;
    for (const QJsonObject& d : drivers) {
        const QJsonArray pos = d.value(QLatin1String("pos_xz")).toArray();
        if (pos.size() < 2 || jb(d, "dnf") || jb(d, "dsq") || jb(d, "in_pit"))
            continue;
        if (isQuali && ji(d, "driver_status") == 0)
            continue;                                      // Garage -> nicht auf die Karte
        const int idx = ji(d, "index");
        const double wx = pos.at(0).toDouble(), wz = pos.at(1).toDouble();
        const auto rec = m_move.constFind(idx);
        if (rec == m_move.cend()) {
            m_move[idx] = Move{wx, wz, nowMs};
        } else if (std::hypot(wx - rec.value().x, wz - rec.value().z) > STOPPED_M) {
            m_move[idx] = Move{wx, wz, nowMs};
        } else if (nowMs - rec.value().t > STOPPED_MS) {
            continue;                                      // steht (geparkt/Crash) -> weg
        }
        const auto p = norm(wx, wz);
        if (!p)
            continue;
        const bool focused = idx == focusIndex;
        QVariantMap c;
        c.insert("key", idx);
        c.insert("x", p->x());
        c.insert("y", p->y());
        c.insert("r", (focused ? 3.4 : 2.9) * dot);
        c.insert("color", Theme::teamColors().value(js(d, "team"), QStringLiteral("#ffffff")));
        c.insert("focused", focused);
        c.insert("retired", false);
        c.insert("opacity", 1.0);
        c.insert("label", showNumbers && ji(d, "position") > 0 ? QString::number(ji(d, "position")) : QString());
        cars.append(c);
    }

    for (auto it = m_ghost.begin(); it != m_ghost.end();) {
        const double age = nowMs - it.value().t;
        if (age >= DNF_LINGER_MS) {
            m_ghostDone.insert(it.key());
            it = m_ghost.erase(it);
            continue;
        }
        const auto p = norm(it.value().x, it.value().z);
        if (p) {
            QVariantMap c;
            c.insert("key", it.key());
            c.insert("x", p->x());
            c.insert("y", p->y());
            c.insert("r", 2.9 * dot);
            c.insert("color", it.value().color);
            c.insert("focused", false);
            c.insert("retired", true);
            c.insert("opacity", 1.0 - age / DNF_LINGER_MS);
            c.insert("label", QString());
            cars.append(c);
        }
        ++it;
    }

    if (cars != m_cars) {
        m_cars = cars;
        emit carsChanged();
    }
}

QList<Trackmap::Pt> Trackmap::smoothLoop(const QList<Pt>& pts, int passes)
{
    // Kontur glaetten (smoothLoop in trackmap.js). Nur x und z werden gemittelt -
    // Rundenanteil und Sektor bleiben pro Punkt erhalten.
    if (pts.size() < 8)
        return pts;
    QList<Pt> out = pts;
    const int n = out.size();
    for (int pass = 0; pass < passes; ++pass) {
        const QList<Pt> src = out;
        for (int i = 0; i < n; ++i) {
            const Pt& a = src.at((i - 1 + n) % n);
            const Pt& c = src.at((i + 1) % n);
            out[i].x = a.x * 0.25 + src.at(i).x * 0.5 + c.x * 0.25;
            out[i].z = a.z * 0.25 + src.at(i).z * 0.5 + c.z * 0.25;
        }
    }
    return out;
}

// ── Charts ───────────────────────────────────────────────────────────────────
Charts::Charts(DataSource* source, SharedState& shared, QObject* parent)
    : QObject(parent)
    , m_source(source)
    , m_shared(shared)
{
}

void Charts::update(const QString& mode, double nowMs)
{
    if (mode.isEmpty()) {
        set_isVisible(false);
        return;
    }
    set_isVisible(true);
    if (mode != this->mode()) {
        set_mode(mode);
        m_pending = 0.0;                  // sofort neu zeichnen
    }
    if (mode == QLatin1String("gap"))
        set_title(QStringLiteral(u"Gap-Verlauf (zum Führenden)"));
    else if (mode == QLatin1String("lap"))
        set_title(QStringLiteral(u"Rundenzeiten-Rückstand (zur schnellsten Runde)"));
    else
        set_title(QStringLiteral("Positionsverlauf"));
    if (nowMs - m_pending < REDRAW_MS)
        return;
    m_pending = nowMs;
    if (mode == QLatin1String("pos")) {
        if (m_source)
            m_source->get(QStringLiteral("/api/lap_positions"), [this](const QJsonObject& d) { onPositions(d); });
        if (!m_positions) {
            set_emptyText(QStringLiteral("Noch keine Rundendaten"));
            return;
        }
        QHash<int, Meta> metaMap;
        const QJsonObject drv = m_positions->value(QLatin1String("drivers")).toObject();
        for (auto it = drv.begin(); it != drv.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            metaMap.insert(it.key().toInt(), Meta{js(o, "name"), js(o, "team")});
        }
        build(normalize(m_positions->value(QLatin1String("laps")).toObject()), metaMap, false);
    } else if (mode == QLatin1String("gap")) {
        build(m_shared.gapHist(), meta(), true);
    } else {
        build(lapDeltas(), meta(), true);
    }
}

void Charts::onPositions(const QJsonObject& data)
{
    m_positions = data;
    if (mode() == QLatin1String("pos")) {
        QHash<int, Meta> metaMap;
        const QJsonObject drv = data.value(QLatin1String("drivers")).toObject();
        for (auto it = drv.begin(); it != drv.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            metaMap.insert(it.key().toInt(), Meta{js(o, "name"), js(o, "team")});
        }
        build(normalize(data.value(QLatin1String("laps")).toObject()), metaMap, false);
    }
}

QHash<int, Charts::Meta> Charts::meta() const
{
    QHash<int, Meta> out;
    for (const QJsonObject& d : m_shared.lastDrivers())
        out.insert(ji(d, "index"), Meta{js(d, "name"), js(d, "team")});
    return out;
}

Charts::Laps Charts::lapDeltas() const
{
    // Rundenzeiten als Rueckstand auf die schnellste Runde im Datensatz.
    double best = std::numeric_limits<double>::infinity();
    const Laps& hist = m_shared.lapHist();
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        for (double v : it.value())
            if (v > 0 && v < best)
                best = v;
    if (std::isinf(best))
        return {};
    Laps out;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it) {
        QHash<int, double> row;
        for (auto d = it.value().cbegin(); d != it.value().cend(); ++d)
            if (d.value() > 0)
                row.insert(d.key(), std::round((d.value() - best) * 100.0) / 100.0);
        out.insert(it.key(), row);
    }
    return out;
}

Charts::Laps Charts::normalize(const QJsonObject& lapsObj)
{
    // {Runde: {Fahrerindex: Wert}} - /api/lap_positions schluesselt als Strings.
    Laps out;
    for (auto it = lapsObj.begin(); it != lapsObj.end(); ++it) {
        const QJsonObject row = it.value().toObject();
        QHash<int, double> r;
        for (auto d = row.begin(); d != row.end(); ++d)
            if (!d.value().isNull())
                r.insert(d.key().toInt(), d.value().toDouble());
        out.insert(it.key().toInt(), r);
    }
    return out;
}

void Charts::build(const Laps& data, const QHash<int, Meta>& metaMap, bool zeroBased)
{
    if (data.isEmpty()) {
        set_emptyText(QStringLiteral("Noch keine Rundendaten"));
        set_maxLap(0);
        if (!m_series.isEmpty()) {
            m_series.clear();
            emit seriesChanged();
        }
        return;
    }
    set_emptyText(QString());
    const int maxLap = data.lastKey();
    double maxY = 0.0;
    QSet<int> idxs;
    for (auto it = data.cbegin(); it != data.cend(); ++it) {
        for (auto d = it.value().cbegin(); d != it.value().cend(); ++d) {
            maxY = std::max(maxY, d.value());
            idxs.insert(d.key());
        }
    }
    if (zeroBased)
        maxY = std::max(1.0, std::ceil(maxY / 5) * 5);

    QList<int> sorted(idxs.begin(), idxs.end());
    std::sort(sorted.begin(), sorted.end());
    QVariantList series;
    for (int idx : sorted) {
        const Meta info = metaMap.value(idx, Meta{QStringLiteral("#%1").arg(idx), QString()});
        QVariantList points;
        double last = 0.0;
        for (auto it = data.cbegin(); it != data.cend(); ++it) {
            const auto v = it.value().constFind(idx);
            if (v == it.value().cend())
                continue;
            QVariantMap p;
            p.insert("lap", it.key());
            p.insert("value", v.value());
            points.append(p);
            last = v.value();
        }
        if (points.isEmpty())
            continue;
        QVariantMap s;
        s.insert("name", info.name);
        s.insert("color", Theme::teamColors().value(info.team, QStringLiteral("#ffffff")));
        s.insert("points", points);
        s.insert("last", last);
        series.append(s);
    }

    set_maxLap(maxLap);
    set_maxValue(maxY);
    set_zeroBased(zeroBased);
    m_series = series;
    emit seriesChanged();
}

// ── Championship ─────────────────────────────────────────────────────────────
Championship::Championship(DataSource* source, QObject* parent)
    : QObject(parent)
    , m_source(source)
{
}

void Championship::update(bool wanted, double nowMs, const QString& view)
{
    const bool teams = view == QLatin1String("teams")
        || (view == QLatin1String("wechsel") && static_cast<qint64>(nowMs / 8000.0) % 2 == 1);
    if (teams != m_wantTeams) {
        m_wantTeams = teams;
        if (!m_data.isEmpty())
            build();
    }
    if (!wanted) {
        if (isVisible()) {
            set_isVisible(false);
            m_rows.clear();
            m_last = 0.0;
            emit rowsChanged();
        }
        return;
    }
    if (m_demo) {
        onData(*m_demo);
        return;
    }
    // Das Backend rechnet live (Basis plus Rennposition) -> regelmaessig neu holen.
    if (nowMs - m_last > POLL_MS) {
        m_last = nowMs;
        if (m_source)
            m_source->get(QStringLiteral("/api/championship"), [this](const QJsonObject& d) { onData(d); });
    }
}

void Championship::onData(const QJsonObject& data)
{
    if (!data.value(QLatin1String("standings")).isArray())
        return;
    m_data = data;
    build();
}

void Championship::build()
{
    const QJsonObject& data = m_data;
    // Team-WM nur, wenn der Server sie liefert (ab 0.3.1) und es Teams gibt
    const QJsonArray teamArr = data.value(QLatin1String("teams")).toArray();
    const bool teams = m_wantTeams && !teamArr.isEmpty();
    const QJsonArray standings = teams ? teamArr : data.value(QLatin1String("standings")).toArray();
    QVariantList rows;
    bool live = false;
    // Platz OHNE die Punkte des laufenden Rennens - daraus der Positionswechsel,
    // den die F1-TV-Tabelle als Pfeil zeigt (ab 0.3.1).
    auto basisPlatz = [&standings](int i) {
        const QJsonObject s = standings.at(i).toObject();
        const double basis = jd(s, "total_points") - jd(s, "live_points");
        int platz = 1;
        for (int k = 0; k < standings.size(); ++k) {
            const QJsonObject o = standings.at(k).toObject();
            const double b = jd(o, "total_points") - jd(o, "live_points");
            if (b > basis || (b == basis && k < i))
                ++platz;
        }
        return platz;
    };
    for (int i = 0; i < standings.size() && i < 12; ++i) {
        const QJsonObject s = standings.at(i).toObject();
        QVariantMap r;
        r.insert("pos", i + 1);
        r.insert("name", teams ? js(s, "team") : js(s, "name"));
        r.insert("color", Theme::teamColors().value(js(s, "team"), QStringLiteral("#ffffff")));
        r.insert("logo", teamLogos().value(js(s, "team")));
        r.insert("change", basisPlatz(i) - (i + 1));   // ohne Live-Punkte immer 0
        r.insert("points", jd(s, "total_points"));
        r.insert("livePoints", jd(s, "live_points"));
        if (jd(s, "live_points") > 0)
            live = true;
        rows.append(r);
    }
    const QString titel = js(data, teams ? "teams_title" : "title");
    set_title(titel.isEmpty() ? QStringLiteral("Championship") : titel);
    set_teamView(teams);
    set_live(live);
    set_isVisible(true);
    if (rows != m_rows) {
        m_rows = rows;
        emit rowsChanged();
    }
}

}  // namespace kers::hud
