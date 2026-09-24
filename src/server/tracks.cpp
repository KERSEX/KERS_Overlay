#include "tracks.h"

#include "constants.h"
#include "pyconv.h"
#include "jsonfile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace kers::server {

namespace {

double abstand(const TrackPt& a, const TrackPt& b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

double round1(double v) { return py::round(v, 1); }
double round4(double v) { return py::round(v, 4); }

}  // namespace

QList<TrackPt> konturAufEineRunde(const QList<TrackPt>& pts, int laenge)
{
    // ⚠ Der Rundenanteil allein taugt dafuer NICHT (er springt je Runde einmal
    // zurueck, aber wie oft das in der Liste steht, haengt davon ab, WO die
    // Aufzeichnung begann). Die GEFAHRENE STRECKE ist eindeutig: eine saubere
    // Runde misst 98-99 % der Streckenlaenge, zwei Runden rund 198 %.
    if (laenge <= 0 || pts.size() < 3)
        return pts;
    double weg = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        weg += abstand(pts.at(i), pts.at(i - 1));
    if (weg < laenge * 1.5)
        return pts;

    // Zurueck am Startpunkt, nachdem mehr als eine halbe Runde gefahren wurde.
    // ⚠ Nicht beim ERSTEN Punkt im Umkreis abbrechen, sondern beim naechsten -
    // sonst bleibt ein sichtbarer Spalt in der Karte (Shanghai: 27 m statt 1 m).
    weg = 0.0;
    int bester = -1;
    double besterAbstand = 0.0;
    for (int i = 1; i < pts.size(); ++i) {
        weg += abstand(pts.at(i), pts.at(i - 1));
        if (weg <= laenge * 0.5)
            continue;
        const double a = abstand(pts.at(i), pts.first());
        if (a < 30.0) {
            if (bester < 0 || a < besterAbstand) {
                bester = i;
                besterAbstand = a;
            }
        } else if (bester >= 0) {
            break;             // Umkreis wieder verlassen - der beste steht fest
        }
    }
    return bester >= 0 ? pts.mid(0, bester + 1) : pts;
}

bool rundeTaugt(const QList<TrackPt>& pts, int laenge, bool ungueltig)
{
    // Weil jede neue Runde die vorige ERSETZT, braucht es diese Pruefung.
    if (pts.size() < 60)
        return false;
    if (ungueltig) {
        qInfo() << "[TRACKS] Runde war ungueltig (Track Limits) - nicht uebernommen";
        return false;
    }
    if (laenge <= 0)
        return true;                       // ohne Streckenlaenge nicht pruefbar
    double weg = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        weg += abstand(pts.at(i), pts.at(i - 1));
    if (!(laenge * 0.85 <= weg && weg <= laenge * 1.20)) {
        qInfo() << QStringLiteral("[TRACKS] Runde misst %1 m bei %2 m Strecke - nicht uebernommen").arg(std::lround(weg)).arg(laenge);
        return false;
    }
    return true;
}

QHash<int, TrackEntry> tracksPruefen(const QJsonObject& roh)
{
    QHash<int, TrackEntry> raus;
    if (roh.value(QLatin1String("fmt")).toInt() != TRACKS_FMT)
        return raus;               // ⚠ kein stiller Rueckfall auf "irgendwie lesen"
    for (auto it = roh.begin(); it != roh.end(); ++it) {
        if (it.key() == QLatin1String("fmt") || !it.value().isObject())
            continue;
        bool okId = false;
        const int tid = it.key().toInt(&okId);
        if (!okId)
            continue;
        const QJsonObject e = it.value().toObject();
        const int laenge = e.value(QLatin1String("len")).toInt();
        const QJsonArray pts = e.value(QLatin1String("pts")).toArray();
        if (laenge <= 0 || pts.size() < 20 || pts.size() > TRACK_PTS_MAX)
            continue;
        QList<TrackPt> sauber;
        sauber.reserve(pts.size());
        bool gut = true;
        for (const QJsonValue& v : pts) {
            const QJsonArray p = v.toArray();
            if (p.size() != 4 || !p.at(0).isDouble() || !p.at(1).isDouble() || !p.at(2).isDouble() || !p.at(3).isDouble()) {
                gut = false;
                break;
            }
            sauber.append(TrackPt{p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble(), p.at(3).toInt()});
        }
        if (!gut || sauber.isEmpty())
            continue;
        TrackEntry neu;
        neu.len = laenge;
        neu.pts = konturAufEineRunde(sauber, laenge);
        if (e.value(QLatin1String("rot")).isDouble())
            neu.rot = std::clamp(e.value(QLatin1String("rot")).toInt(), 0, 359);
        if (e.value(QLatin1String("flip")).isBool())
            neu.flip = e.value(QLatin1String("flip")).toBool();
        raus.insert(tid, neu);
    }
    return raus;
}

// ── TrackStore ───────────────────────────────────────────────────────────────
TrackStore::TrackStore(const QString& builtinPath, const QString& userPath)
    : m_builtinPath(builtinPath)
    , m_userPath(userPath)
    , m_builtin(lesen(builtinPath))
    , m_user(lesen(userPath))
{
}

QHash<int, TrackEntry> TrackStore::lesen(const QString& pfad)
{
    return tracksPruefen(json::readObject(pfad));
}

const TrackEntry* TrackStore::eintrag(int trackId) const
{
    auto it = m_user.constFind(trackId);
    if (it != m_user.cend())
        return &it.value();
    auto b = m_builtin.constFind(trackId);
    if (b != m_builtin.cend())
        return &b.value();
    return nullptr;
}

bool TrackStore::laden(int trackId, int trackLength, TrackOutline& outline)
{
    // ⚠ Muss die Streckenlaenge pruefen: dieselbe ID mit anderer Laenge ist eine
    // andere Streckenfuehrung, und eine falsche Karte ist schlimmer als keine.
    const TrackEntry* e = eintrag(trackId);
    if (!e)
        return false;
    if (trackLength > 0 && std::abs(e->len - trackLength) > TRACK_LEN_TOL) {
        qInfo() << QStringLiteral("[TRACKS] #%1: gespeichert %2 m, gemeldet %3 m - verworfen, wird neu gelernt").arg(trackId).arg(e->len).arg(trackLength);
        return false;
    }
    outline.pts = e->pts;
    outline.done = true;
    outline.fest = true;               // bekannt -> nicht mehr neu lernen
    ++outline.ver;
    if (m_anwenden)
        m_anwenden(*e);
    return true;
}

void TrackStore::sichern(int trackId, int trackLength, const TrackOutline& outline)
{
    if (trackId < 0 || outline.pts.isEmpty())
        return;
    TrackEntry e;
    e.len = trackLength;
    e.pts = konturAufEineRunde(outline.pts, trackLength);
    m_user.insert(trackId, e);
    speichern();
    qInfo() << QStringLiteral("[TRACKS] %1 gelernt und gespeichert (%2 Punkte, %3 m)").arg(trackInfo(trackId).name).arg(outline.pts.size()).arg(trackLength);
}

void TrackStore::ausrichtungMerken(int trackId, int rot, bool flip)
{
    // ⚠ Nur bei einer BEKANNTEN Strecke. Nur mitgeliefert? Dann eine eigene
    // Kopie anlegen - die mitgelieferte steckt in der EXE.
    auto it = m_user.find(trackId);
    if (it == m_user.end()) {
        const auto vorlage = m_builtin.constFind(trackId);
        if (vorlage == m_builtin.cend())
            return;
        TrackEntry kopie;
        kopie.len = vorlage->len;
        kopie.pts = vorlage->pts;
        it = m_user.insert(trackId, kopie);
    }
    if (it->rot && *it->rot == rot && it->flip && *it->flip == flip)
        return;
    it->rot = rot;
    it->flip = flip;
    speichern();
}

bool TrackStore::loeschen(int trackId)
{
    if (m_user.remove(trackId) == 0)
        return false;
    speichern();
    return true;
}

void TrackStore::speichern()
{
    // Nur die SELBST gelernten. Der Name steht NUR fuer den Menschen drin, der
    // die Datei aufmacht - gelesen wird er nie (massgeblich ist trackInfo).
    QJsonObject raus;
    raus["fmt"] = TRACKS_FMT;
    for (auto it = m_user.cbegin(); it != m_user.cend(); ++it) {
        QJsonObject e;
        e["name"] = trackInfo(it.key()).name;
        e["len"] = it->len;
        if (it->rot)
            e["rot"] = *it->rot;
        if (it->flip)
            e["flip"] = *it->flip;
        QJsonArray pts;
        for (const TrackPt& p : it->pts)
            pts.append(QJsonArray{round1(p.x), round1(p.z), round4(p.frac), p.sector});
        e["pts"] = pts;
        raus[QString::number(it.key())] = e;
    }
    QSaveFile f(m_userPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[TRACKS] Speichern fehlgeschlagen:" << m_userPath;
        return;
    }
    f.write(QJsonDocument(raus).toJson(QJsonDocument::Compact));
    f.commit();
}

// ── TrackLearner ─────────────────────────────────────────────────────────────
void TrackLearner::refReset()
{
    m_ref = Ref{};
    m_buf.clear();
}

void TrackLearner::sessionNeu()
{
    // ⚠ Bewusst anders als 0.2.6: main.py leerte hier IMMER die Karte. Bei einer
    // bekannten Strecke ("fest") blieb sie danach leer - fest sperrt das Lernen, und
    // dieselbe track_id loest keinen Streckenwechsel aus (Quali -> Rennen). Eine
    // bekannte Kontur bleibt deshalb stehen; wechselt die Strecke wirklich, laedt
    // das naechste Session-Paket ueber streckeGewechselt() die richtige.
    refReset();
    if (m_outline.fest)
        return;
    m_outline.pts.clear();
    m_outline.done = false;
    ++m_outline.ver;
}

void TrackLearner::neuLernen()
{
    // ⚠ Die GESPEICHERTE muss der Aufrufer mit loeschen (TrackStore::loeschen),
    // sonst laedt sie sich beim naechsten Streckenwechsel sofort wieder.
    m_outline.pts.clear();
    m_outline.done = false;
    m_outline.fest = false;             // ab jetzt darf wieder gelernt werden
    ++m_outline.ver;
    refReset();
}

bool TrackLearner::streckeGewechselt(int trackId, int trackLength)
{
    // Kennen wir sie schon, steht die Karte sofort - ohne dass jemand erst eine
    // Runde fahren muss. Sonst leeren und lernen.
    refReset();
    if (m_store.laden(trackId, trackLength, m_outline))
        return true;
    m_outline.pts.clear();
    m_outline.done = false;
    m_outline.fest = false;
    ++m_outline.ver;
    return false;
}

void TrackLearner::learn(const QHash<int, QPointF>& carPos, const QList<LearnerCar>& cars, int tl, int trackId)
{
    // ⚠ Eine bekannte Strecke wird NICHT angefasst. Gelernt wird nur, was fehlt.
    if (m_outline.fest)
        return;

    const LearnerCar* ref = nullptr;
    if (m_ref.idx >= 0)
        for (const LearnerCar& c : cars)
            if (c.index == m_ref.idx) {
                ref = &c;
                break;
            }
    // m_driverStatus ist im Rennen unbrauchbar (fuer alle 2 = "in lap"), daher NUR
    // ueber in_pit/Position filtern. Die Boxengasse faellt ueber in_pit raus.
    const bool refOk = ref && carPos.contains(m_ref.idx) && !ref->inPit && !ref->dnf && !ref->dsq
        && !(!m_ref.startProg && tl > 0);       // Streckenlaenge kam nach
    if (!refOk) {
        // (Neu-)Wahl der Referenz: bestplatziertes Auto auf der Strecke. Nur der
        // PUFFER wird geleert - eine schon fertige Karte bleibt stehen.
        refReset();
        QList<const LearnerCar*> sorted;
        for (const LearnerCar& c : cars)
            sorted.append(&c);
        std::stable_sort(sorted.begin(), sorted.end(), [](const LearnerCar* a, const LearnerCar* b) {
            return (a->position > 0 ? a->position : 99) < (b->position > 0 ? b->position : 99);
        });
        for (const LearnerCar* c : sorted) {
            if (c->position > 0 && carPos.contains(c->index) && !c->inPit) {
                const QPointF p = carPos.value(c->index);
                m_ref.idx = c->index;
                m_ref.startPt = p;
                m_ref.last = p;
                m_ref.dist = 0.0;
                m_ref.hasStart = true;
                m_ref.invalid = false;
                if (tl > 0)
                    m_ref.startProg = c->lapNum + std::max(0.0, c->lapDistance) / tl;
                const double frac = tl > 0 ? round4(std::max(0.0, c->lapDistance) / tl) : -1;
                m_buf = {TrackPt{p.x(), p.y(), frac, c->sector}};
                break;
            }
        }
        return;
    }

    // Track Limits waehrend DIESER Runde? Dann taugt sie nicht als Vorlage.
    if (ref->lapInvalid)
        m_ref.invalid = true;
    const QPointF p = carPos.value(m_ref.idx);
    const double dx = p.x() - m_ref.last.x(), dz = p.y() - m_ref.last.y();
    const double step2 = dx * dx + dz * dz;
    if (step2 >= 25.0) {                          // >= 5 m seit dem letzten Punkt
        const double frac = tl > 0 ? round4(std::max(0.0, ref->lapDistance) / tl) : -1;
        m_buf.append(TrackPt{p.x(), p.y(), frac, ref->sector});
        m_ref.dist += std::sqrt(step2);
        m_ref.last = p;
    }

    // Schliessen: GENAU eine volle Runde. Bevorzugt ueber den Fortschritt
    // (Runde + lap_distance/Streckenlaenge). Ohne Streckenlaenge Rueckfall ueber
    // Distanz und Naehe.
    bool closed = false;
    if (tl > 0 && m_ref.startProg) {
        const double prog = ref->lapNum + std::max(0.0, ref->lapDistance) / tl;
        if (prog - *m_ref.startProg >= 1.0 && m_buf.size() > 60)
            closed = true;
    } else {
        const double sx = p.x() - m_ref.startPt.x(), sz = p.y() - m_ref.startPt.y();
        if (m_ref.dist > 1000.0 && sx * sx + sz * sz < 900.0)
            closed = true;
    }
    // Notbremse ueber die GEFAHRENE Strecke: lap_num und lap_distance koennen an
    // der Start/Ziel-Linie auseinanderliegen - so entstand Shanghai mit 1917
    // Punkten und 10,7 km.
    if (!closed && tl > 0 && m_ref.dist > tl * 1.25)
        closed = true;

    if (!closed) {
        if (m_buf.size() > TRACK_PTS_MAX)           // Sicherheitsnetz: wegwerfen, neu ansetzen
            m_ref.idx = -1;
        return;
    }

    if (rundeTaugt(m_buf, tl, m_ref.invalid)) {
        m_outline.pts = m_buf;
        m_outline.done = true;
        ++m_outline.ver;
        m_store.sichern(trackId, tl, m_outline);
    }
    // Ab hier die naechste Runde - Startpunkt ist der aktuelle.
    const double frac = tl > 0 ? round4(std::max(0.0, ref->lapDistance) / tl) : -1;
    m_ref.startPt = p;
    m_ref.last = p;
    m_ref.dist = 0.0;
    m_ref.invalid = false;
    m_ref.startProg = tl > 0 ? std::optional<double>(ref->lapNum + std::max(0.0, ref->lapDistance) / tl) : std::nullopt;
    m_buf = {TrackPt{p.x(), p.y(), frac, ref->sector}};
}

}  // namespace kers::server
