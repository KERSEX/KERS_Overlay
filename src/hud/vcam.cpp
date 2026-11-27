#include "vcam.h"

#include "extras.h"
#include "theme.h"
#include "vcam_mesh.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

constexpr double PI = 3.14159265358979323846;

const VcCar* finde(const QList<VcCar>& autos, int idx)
{
    for (const VcCar& c : autos)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

/// Rang eines Motivs - hoeher schlaegt niedriger.
int rang(const VcMotiv& m, double engsterAbstand)
{
    switch (m.art) {
    case VcMotiv::Ueberholen: return 5;
    case VcMotiv::Hotlap: return 4;
    case VcMotiv::Kampf: return engsterAbstand < VcDirector::KAMPF_ENG_S ? 4 : 2;
    case VcMotiv::Box: return 3;
    case VcMotiv::Fuehrender:
    case VcMotiv::Fahrer: return 1;
    default: return 0;
    }
}

double winkelDiff(double a, double b)
{
    double d = a - b;
    while (d > PI)
        d -= 2 * PI;
    while (d < -PI)
        d += 2 * PI;
    return d;
}

QVector3D vorwaerts(double kurs)
{
    return QVector3D(float(std::sin(kurs)), 0.f, float(std::cos(kurs)));
}

}  // namespace

// ── Auto-Regie ───────────────────────────────────────────────────────────────
void VcDirector::reset()
{
    m_motiv = VcMotiv{};
    m_seit = 0;
    m_platz.clear();
    m_manoever.clear();
    m_gesperrt.clear();
}

bool VcDirector::sichtbar(const VcCar& c, const VcCar* fokus, double streckeM, bool warGesperrt) const
{
    if (!fokus)
        return false;
    if (c.idx == fokus->idx)
        return true;
    // Raum, nicht Rennposition: ein Ueberrundeter neben dem Fokus ist genauso im Bild.
    const double f = warGesperrt ? freiFaktor : 1.0;
    if ((c.p - fokus->p).length() < sichtLuftM * f)
        return true;
    if (streckeM > 0) {
        double d = std::fmod(std::abs(c.lapDist - fokus->lapDist), streckeM);
        d = std::min(d, streckeM - d);
        if (d < sichtStreckeM * f)
            return true;
    }
    return false;
}

void VcDirector::merkeUeberholen(const QList<VcCar>& autos, double nowMs, double streckeM)
{
    // Positionstausch zweier Autos: A war hinter B, jetzt hat A Bs alten Platz und
    // B den von A. Boxenstopps ausgenommen - dort verliert einer viele Plaetze auf
    // einmal, ohne dass irgendwo ueberholt wurde.
    // Ausserdem nur nah beieinander auf der Strecke (Strafen, Rueckblenden und
    // Ausfaelle tauschen auch Plaetze) und nicht unter SC/VSC/Start.
    for (const VcCar& a : autos) {
        const int vorher = m_platz.value(a.idx, 0);
        if (!kaempfe || vorher <= 0 || a.pos <= 0 || a.pos >= vorher || a.inPit)
            continue;
        for (const VcCar& b : autos) {
            if (b.idx == a.idx || b.inPit || m_platz.value(b.idx, 0) != a.pos || b.pos != vorher)
                continue;
            double d = std::abs(a.lapDist - b.lapDist);
            if (streckeM > 0) {
                d = std::fmod(d, streckeM);
                d = std::min(d, streckeM - d);
            }
            if (d < 60)
                m_manoever.append({a.idx, b.idx, a.pos, nowMs});
        }
    }
    m_platz.clear();
    for (const VcCar& c : autos)
        m_platz.insert(c.idx, c.pos);
    m_manoever.erase(std::remove_if(m_manoever.begin(), m_manoever.end(),
                                    [nowMs](const Manoever& m) { return nowMs - m.t > UEBERHOL_MS; }),
                     m_manoever.end());
}

QList<QPair<int, VcMotiv>> VcDirector::kandidaten(const QList<VcCar>& autos, const QSet<int>& gesperrt,
                                                  bool quali, double) const
{
    QList<QPair<int, VcMotiv>> k;
    auto frei = [&](int idx) { return !gesperrt.contains(idx) && finde(autos, idx); };

    QList<VcCar> nachPlatz = autos;
    std::stable_sort(nachPlatz.begin(), nachPlatz.end(),
                     [](const VcCar& a, const VcCar& b) { return a.pos < b.pos; });

    if (quali) {
        // Schnelle Runden: wer am weitesten ist, kommt zuerst ins Ziel.
        QList<VcCar> heiss;
        for (const VcCar& c : nachPlatz)
            if (c.hotlap && frei(c.idx))
                heiss.append(c);
        std::stable_sort(heiss.begin(), heiss.end(),
                         [](const VcCar& a, const VcCar& b) { return a.lapDist > b.lapDist; });
        for (const VcCar& c : heiss)
            k.append({4, VcMotiv{VcMotiv::Hotlap, {c.idx}, c.pos}});
    } else if (kaempfe) {
        for (const Manoever& m : m_manoever)
            if (frei(m.vorn) && frei(m.hinten))
                k.append({5, VcMotiv{VcMotiv::Ueberholen, {m.vorn, m.hinten}, m.platz}});

        // Kampfgruppen: aufeinanderfolgende Plaetze mit weniger als KAMPF_S Abstand.
        for (int i = 0; i < nachPlatz.size();) {
            int j = i;
            double eng = 99;
            while (j + 1 < nachPlatz.size() && nachPlatz[j + 1].pos == nachPlatz[j].pos + 1
                   && nachPlatz[j + 1].gapAhead > 0 && nachPlatz[j + 1].gapAhead < KAMPF_S
                   && !nachPlatz[j].inPit && !nachPlatz[j + 1].inPit) {
                eng = std::min(eng, nachPlatz[j + 1].gapAhead);
                ++j;
            }
            if (j > i) {
                VcMotiv m{VcMotiv::Kampf, {}, nachPlatz[i].pos};
                bool ok = true;
                for (int x = i; x <= j; ++x) {
                    m.autos.append(nachPlatz[x].idx);
                    ok = ok && frei(nachPlatz[x].idx);
                }
                if (ok)
                    k.append({rang(m, eng), m});
            }
            i = j + 1;
        }
    }
    if (!quali) {
        for (const VcCar& c : nachPlatz)
            if (c.inPit && frei(c.idx))
                k.append({3, VcMotiv{VcMotiv::Box, {c.idx}, c.pos}});
        for (const VcCar& c : nachPlatz) {
            if (frei(c.idx)) {
                k.append({1, VcMotiv{c.pos == 1 ? VcMotiv::Fuehrender : VcMotiv::Fahrer, {c.idx}, c.pos}});
                break;
            }
        }
    }
    std::stable_sort(k.begin(), k.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    return k;
}

bool VcDirector::gueltig(const VcMotiv& m, const QList<VcCar>& autos, const QSet<int>& gesperrt, bool,
                         double nowMs) const
{
    if (m.art == VcMotiv::Keins)
        return false;
    QList<const VcCar*> cs;
    for (int idx : m.autos) {
        const VcCar* c = finde(autos, idx);
        if (!c || gesperrt.contains(idx))
            return false;          // weg oder jetzt im Spielbild -> sofort weg
        cs.append(c);
    }
    switch (m.art) {
    case VcMotiv::Kampf:
        // Nach der AKTUELLEN Reihenfolge pruefen - nach einem Ueberholen ist es derselbe Kampf.
        if (!kaempfe)
            return false;
        std::sort(cs.begin(), cs.end(), [](const VcCar* a, const VcCar* b) { return a->pos < b->pos; });
        for (int i = 1; i < cs.size(); ++i)
            if (cs[i]->pos != cs[i - 1]->pos + 1 || cs[i]->inPit || cs[i - 1]->inPit
                || cs[i]->gapAhead > KAMPF_S + 0.5)
                return false;
        return true;
    case VcMotiv::Ueberholen: return kaempfe && nowMs - m_seit < HALT_MIN_MS;
    case VcMotiv::Box: return cs.first()->inPit;
    case VcMotiv::Hotlap: return cs.first()->hotlap;
    case VcMotiv::Fuehrender: return cs.first()->pos == 1;
    default: return true;
    }
}

VcMotiv VcDirector::waehle(const QList<VcCar>& autos, int focus, bool quali, double streckeM, double nowMs)
{
    merkeUeberholen(autos, nowMs, streckeM);

    // Fokus unbekannt (Index 255) -> man weiss nicht, was im Spielbild ist. Dann
    // lieber gar nichts zeigen (Keins = Bild-im-Bild aus).
    const VcCar* fokus = finde(autos, focus);
    if (!fokus) {
        m_gesperrt.clear();
        if (m_motiv.art != VcMotiv::Keins) {
            m_motiv = VcMotiv{};
            m_seit = nowMs;
        }
        return m_motiv;
    }
    QSet<int> gesperrt;
    for (const VcCar& c : autos)
        if (sichtbar(c, fokus, streckeM, m_gesperrt.contains(c.idx)))
            gesperrt.insert(c.idx);
    m_gesperrt = gesperrt;

    // Ueberholen im gerade gezeigten Kampf: nur die Unterschrift wechselt (die Plaetze
    // aendern sich erst NACH dem Manoever - als neues Motiv kaeme es zu spaet).
    if (m_motiv.art == VcMotiv::Kampf) {
        for (const Manoever& m : m_manoever) {
            if (m.t >= nowMs && m_motiv.autos.contains(m.vorn) && m_motiv.autos.contains(m.hinten)
                && !gesperrt.contains(m.vorn) && !gesperrt.contains(m.hinten)) {
                m_motiv = VcMotiv{VcMotiv::Ueberholen, {m.vorn, m.hinten}, m.platz};
                m_seit = nowMs;
                return m_motiv;
            }
        }
    }

    const auto k = kandidaten(autos, gesperrt, quali, nowMs);
    const double gehalten = nowMs - m_seit;
    int rangJetzt = 0;
    for (const auto& [r, m] : k)
        if (m == m_motiv)
            rangJetzt = r;

    // Nichts frei (alle im Spielbild) -> Keins, das Bild geht aus.
    VcMotiv neu = m_motiv;
    if (!gueltig(m_motiv, autos, gesperrt, quali, nowMs)) {
        neu = k.isEmpty() ? VcMotiv{} : k.first().second;
    } else if (gehalten >= HALT_MIN_MS && !k.isEmpty()) {
        if (k.first().first > rangJetzt) {
            neu = k.first().second;
        } else if (gehalten >= HALT_MAX_MS) {
            for (const auto& [r, m] : k) {
                if (m != m_motiv) {
                    neu = m;
                    break;
                }
            }
        }
    }
    if (neu != m_motiv) {
        m_motiv = neu;
        m_seit = nowMs;
    }
    return m_motiv;
}

QString vcAutoKamera(VcMotiv::Art art, double gehaltenMs)
{
    // Kampf: TV, Heckkamera des Vorderen (zeigt den Angreifer), Verfolger, Onboard des Angreifers.
    static const QStringList kampf = {"tv", "heck", "verfolger", "onboard"};
    static const QStringList runde = {"verfolger", "onboard", "tv", "heli"};
    static const QStringList box = {"heli", "vogel"};
    static const QStringList fahrer = {"heli", "verfolger", "onboard", "tv", "vogel"};
    const QStringList& folge = (art == VcMotiv::Kampf || art == VcMotiv::Ueberholen) ? kampf
                               : art == VcMotiv::Hotlap                               ? runde
                               : art == VcMotiv::Box                                  ? box
                                                                                      : fahrer;
    return folge.at(int(std::max(0.0, gehaltenMs) / VcDirector::HALT_MIN_MS) % folge.size());
}

// ── TV-Kameras ───────────────────────────────────────────────────────────────
QList<VcTvCam> vcTvKameras(const QList<QVector3D>& p, const QList<double>& fracs, double abstandM)
{
    QList<VcTvCam> cams;
    const int n = int(p.size());
    if (n < 10)
        return cams;
    double gesamt = 0;
    for (int i = 0; i < n; ++i)
        gesamt += (p[(i + 1) % n] - p[i]).length();
    const int k = std::max(1, int(std::lround(30.0 / std::max(1.0, gesamt / n))));   // ~30 m Hebel
    double weg = 0, naechste = abstandM / 2;
    bool seite = false;
    for (int i = 0; i < n; ++i) {
        weg += (p[(i + 1) % n] - p[i]).length();
        if (weg < naechste)
            continue;
        naechste += abstandM;
        const QVector3D t0 = (p[i] - p[(i - k + n) % n]).normalized();
        const QVector3D t1 = (p[(i + k) % n] - p[i]).normalized();
        const QVector3D t = (t0 + t1).normalized();
        const QVector3D links(t.z(), 0, -t.x());
        const float kruemmung = t0.z() * t1.x() - t0.x() * t1.z();   // > 0: Linkskurve
        QVector3D aussen;
        if (std::abs(kruemmung) > 0.05f)
            aussen = kruemmung > 0 ? -links : links;
        else
            aussen = (seite = !seite) ? links : -links;               // Gerade: abwechselnd
        cams.append({p[i] + aussen.normalized() * 28.f + QVector3D(0, 9, 0),
                     i < fracs.size() && fracs[i] >= 0 ? fracs[i] : double(i) / n});
    }
    return cams;
}

// ── Steuerung ────────────────────────────────────────────────────────────────
VirtualCam::VirtualCam(SharedState& shared, Config& cfg, Trackmap* track, QObject* parent)
    : QObject(parent)
    , m_shared(shared)
    , m_cfg(cfg)
    , m_track(track)
    , m_hSumme(BINS, 0.0)
    , m_hAnzahl(BINS, 0.0)
    , m_autoNetz(vcCarMesh())
{
    connect(&m_timer, &QTimer::timeout, this, &VirtualCam::tick);
    m_uhr.start();
}

void VirtualCam::update(const QList<QJsonObject>& drivers, const QJsonObject& session, int focus, bool connected,
                        bool an, const QString& kameraWunsch, int ziel)
{
    // Neue Strecke -> Hoehenprofil vergessen (Quali -> Rennen behaelt es).
    // Neue Strecke oder Session-Art -> Regie und Glaettung neu.
    const double strecke = jd(session, "track_length");
    const bool quali = jb(session, "is_quali");
    if (std::abs(strecke - m_streckeM) > 1) {
        std::fill(m_hSumme.begin(), m_hSumme.end(), 0.0);
        std::fill(m_hAnzahl.begin(), m_hAnzahl.end(), 0.0);
        m_hBekannt = 0;
        m_hGebaut = -1;
    }
    if (std::abs(strecke - m_streckeM) > 1 || quali != m_quali) {
        m_regie.reset();
        m_zustand.clear();
    }
    m_streckeM = strecke;
    m_quali = quali;
    // Kein Kampf/Ueberholen unter SC/VSC/Einfuehrungsrunde und in den ersten zwei
    // Runden - dann ist das Feld dicht und alles waere ein "Kampf" (wie Battles).
    m_regie.kaempfe = js(session, "safety_car_status", QStringLiteral("none")) == QLatin1String("none")
                      && ji(session, "current_lap") >= 3;
    m_fokus = focus;
    m_kameraWunsch = kameraWunsch.isEmpty() ? QStringLiteral("auto") : kameraWunsch;
    m_ziel = ziel;

    m_autos.clear();
    QSet<int> da;
    for (const QJsonObject& d : drivers) {
        const QJsonArray xz = d.value(QLatin1String("pos_xz")).toArray();
        if (xz.size() < 2 || jb(d, "dnf") || jb(d, "dsq"))
            continue;
        VcCar c;
        c.idx = ji(d, "index");
        c.pos = ji(d, "position");
        c.name = js(d, "name");
        c.p = QVector3D(float(xz.at(0).toDouble()), float(jd(d, "pos_y")), float(xz.at(1).toDouble()));
        c.lapDist = jd(d, "lap_distance");
        c.gapAhead = jd(d, "gap_to_ahead");
        c.inPit = jb(d, "in_pit");
        c.hotlap = m_quali && m_shared.qualiStatus(d) == QLatin1String("track");
        m_autos.append(c);
        da.insert(c.idx);

        Zustand& z = m_zustand[c.idx];
        z.soll = c.p;
        if (d.contains(QLatin1String("heading")))
            z.kursSoll = jd(d, "heading");
        else if (!z.neu && (c.p - z.letzte).length() > 0.5f)
            z.kursSoll = std::atan2(double(c.p.x() - z.letzte.x()), double(c.p.z() - z.letzte.z()));
        z.letzte = c.p;
        const QColor farbe(Theme::teamColors().value(js(d, "team"), QStringLiteral("#ffffff")));
        z.farbe = QVector4D(float(farbe.redF()), float(farbe.greenF()), float(farbe.blueF()), 1.f);
        z.frac = m_streckeM > 0 ? std::fmod(std::max(0.0, c.lapDist) / m_streckeM, 1.0) : -1;
        if (z.neu) {
            z.ist = z.soll;
            z.kursIst = z.kursSoll;
            z.neu = false;
        }
        // Hoehenprofil: nur fahrende Autos auf der Strecke
        if (!c.inPit && z.frac >= 0 && d.contains(QLatin1String("pos_y"))) {
            const int b = std::clamp(int(z.frac * BINS), 0, BINS - 1);
            if (m_hAnzahl[b] == 0)
                ++m_hBekannt;
            m_hAnzahl[b] = std::min(m_hAnzahl[b] + 1, 30.0);
            m_hSumme[b] += (c.p.y() - m_hSumme[b]) / m_hAnzahl[b];   // laufender Mittelwert
        }
    }
    for (auto it = m_zustand.begin(); it != m_zustand.end();)
        it = da.contains(it.key()) ? std::next(it) : m_zustand.erase(it);

    // Motiv: Regie-Ziel oder Auto-Regie
    const double now = double(m_uhr.elapsed());
    const VcCar* manuell = finde(m_autos, ziel);
    m_motiv = manuell ? VcMotiv{VcMotiv::Fahrer, {ziel}, manuell->pos}
                      : m_regie.waehle(m_autos, focus, m_quali, m_streckeM, now);
    // Andere Autos -> Kamerafolge von vorn; Kampf -> Ueberholen derselben Autos laeuft weiter.
    QList<int> motivAutos = m_motiv.autos;
    std::sort(motivAutos.begin(), motivAutos.end());
    if (motivAutos != m_motivAutos) {
        m_motivAutos = motivAutos;
        m_motivSeit = now;
    }
    m_kameraArt = m_kameraWunsch != QLatin1String("auto") ? m_kameraWunsch
                                                          : vcAutoKamera(m_motiv.art, now - m_motivSeit);

    // Bildunterschrift
    auto name = [this](int idx) {
        const VcCar* c = finde(m_autos, idx);
        return c ? c->name.toUpper() : QString();
    };
    QString t;
    switch (m_motiv.art) {
    case VcMotiv::Kampf: t = QStringLiteral("FIGHT FOR P%1").arg(m_motiv.fuerPlatz); break;
    case VcMotiv::Ueberholen: t = QStringLiteral("OVERTAKE FOR P%1 · %2").arg(m_motiv.fuerPlatz).arg(name(m_motiv.autos.value(0))); break;
    case VcMotiv::Box: t = QStringLiteral("PIT STOP · %1").arg(name(m_motiv.autos.value(0))); break;
    case VcMotiv::Hotlap: t = QStringLiteral("HOT LAP · %1").arg(name(m_motiv.autos.value(0))); break;
    case VcMotiv::Fuehrender: t = QStringLiteral("LEADER · %1").arg(name(m_motiv.autos.value(0))); break;
    case VcMotiv::Fahrer: t = QStringLiteral("P%1 · %2").arg(m_motiv.fuerPlatz).arg(name(m_motiv.autos.value(0))); break;
    default: break;
    }
    set_titel(t);
    static const QHash<QString, QString> kameraNamen = {
        {"vogel", "BIRD'S EYE"}, {"heli", "HELI CAM"}, {"verfolger", "CHASE CAM"},
        {"onboard", "ONBOARD"}, {"heck", "REAR CAM"}, {"tv", "TV CAM"}};
    set_kamera(kameraNamen.value(m_kameraArt, m_kameraArt.toUpper()));

    // Sichtbar + Uhr
    // Eingefrorenes Quali-Ergebnis: nichts faehrt mehr, Bild-im-Bild weg.
    // Keins (alles im Spielbild, Fokus unbekannt) -> aus.
    const bool sichtbar = an && connected && m_motiv.art != VcMotiv::Keins && !jb(session, "_quali_result");
    set_isVisible(sichtbar);
    static const QList<int> erlaubt = {20, 25, 30, 40, 60, 120};
    const int fps = erlaubt.contains(m_cfg.i("vcam_fps")) ? m_cfg.i("vcam_fps") : 30;
    if (sichtbar) {
        if (m_timer.interval() != 1000 / fps || !m_timer.isActive())
            m_timer.start(1000 / fps);
    } else {
        m_timer.stop();
    }
}

void VirtualCam::profilRechnen()
{
    // Bekannte Bins uebernehmen, Luecken linear (ueber Start/Ziel hinweg) fuellen,
    // dann leicht glaetten. Keine Daten -> flach auf 0.
    m_bandH = QVector<double>(BINS, 0.0);
    QList<int> bekannt;
    for (int i = 0; i < BINS; ++i)
        if (m_hAnzahl[i] > 0)
            bekannt.append(i);
    if (bekannt.isEmpty())
        return;
    for (int k = 0; k < bekannt.size(); ++k) {
        const int a = bekannt[k], b = bekannt[(k + 1) % bekannt.size()];
        const int lang = ((b - a + BINS - 1) % BINS) + 1;   // Schritte von a bis b (ganze Runde bei nur einem)
        for (int s = 0; s < lang; ++s)
            m_bandH[(a + s) % BINS] = m_hSumme[a] + (m_hSumme[b] - m_hSumme[a]) * s / lang;
    }
    QVector<double> glatt(BINS);
    for (int i = 0; i < BINS; ++i) {
        double sum = 0;
        for (int d = -3; d <= 3; ++d)
            sum += m_bandH[(i + d + BINS) % BINS];
        glatt[i] = sum / 7;
    }
    m_bandH = glatt;
}

double VirtualCam::hoehe(double frac) const
{
    if (m_bandH.isEmpty() || frac < 0)
        return 0.0;
    const double f = std::fmod(frac, 1.0) * BINS;
    const int i = std::clamp(int(f), 0, BINS - 1);
    const double t = f - i;
    return m_bandH[i] + (m_bandH[(i + 1) % BINS] - m_bandH[i]) * t;
}

void VirtualCam::baueStrecke()
{
    // Kontur aus dem Trackmap-Baustein (pollt /api/track ohnehin), Hoehe aus dem Profil.
    const auto& pts = m_track ? m_track->points() : QList<Trackmap::Pt>{};
    profilRechnen();
    m_kontur.clear();
    m_konturFrac.clear();
    int start = 0;
    double startFrac = 2;
    for (int i = 0; i < pts.size(); ++i) {
        const double f = pts[i].frac >= 0 ? pts[i].frac : double(i) / pts.size();
        m_kontur.append(QVector3D(float(pts[i].x), float(hoehe(f)), float(pts[i].z)));
        m_konturFrac.append(f);
        if (pts[i].frac >= 0 && pts[i].frac < startFrac) {
            startFrac = pts[i].frac;
            start = i;
        }
    }
    m_streckenNetz = vcStreckenNetz(m_kontur, start);
    const auto alt = m_tv.size();
    m_tv = vcTvKameras(m_kontur, m_konturFrac, 260.0);
    if (m_tv.size() != alt)
        m_tvAktiv = -1;
    m_konturVer = m_track ? m_track->version() : -1;
    m_hGebaut = m_hBekannt;
    m_gebautMs = double(m_uhr.elapsed());
}

QMatrix4x4 VirtualCam::modell(const Zustand& z)
{
    QMatrix4x4 m;
    m.translate(z.ist);
    m.rotate(float(z.kursIst * 180.0 / PI), 0.f, 1.f, 0.f);
    m.rotate(float(-z.neigung * 180.0 / PI), 1.f, 0.f, 0.f);   // Drehung um x: -Winkel = Nase hoch
    return m;
}

void VirtualCam::kameraRechnen(double dt, bool schnitt)
{
    QList<const Zustand*> zs;
    for (int idx : m_motiv.autos)
        if (m_zustand.contains(idx))
            zs.append(&m_zustand[idx]);
    if (zs.isEmpty())
        return;                          // nichts zu zeigen - update() hat das Bild schon aus
    const QString& art = m_kameraArt;

    QVector3D ziel;
    for (const Zustand* z : zs)
        ziel += z->ist;
    ziel /= float(zs.size());
    float spanne = 0;                    // wie weit die Motiv-Autos auseinander sind
    for (const Zustand* z : zs)
        spanne = std::max(spanne, (z->ist - ziel).length() * 2.f);
    const Zustand* vorn = zs.first();
    const Zustand* hinten = zs.last();   // Angreifer im Kampf
    const QVector3D oben(0, 1, 0);
    const bool amAuto = art == QLatin1String("onboard") || art == QLatin1String("heck");

    if (amAuto) {
        // Fest am Auto, mit Neigung. Onboard = T-Kamera ueber dem Helm: Blick nach vorn
        // ueber Halo, Lenkrad und Vorderreifen. Heck = auf der Airbox, Blick zurueck ueber
        // den Heckfluegel (im Kampf am Vorderen - zeigt den Angreifer).
        const bool heck = art == QLatin1String("heck");
        const Zustand& z = heck ? *vorn : *hinten;
        const QMatrix4x4 m = modell(z);
        m_augeSoll = m.map(heck ? QVector3D(0, 1.45f, -0.35f) : QVector3D(0, 1.25f, -0.42f));
        m_ziel3Soll = m.map(heck ? QVector3D(0, -0.67f, -12.35f) : QVector3D(0, -0.69f, 9.58f));   // 10-11 Grad nach unten
        m_fovSoll = heck ? 58.f : 52.f;
        m_kamKurs = z.kursIst;
        schnitt = true;                  // haengt starr am Auto, nichts nachziehen
    } else {
        // Blickrichtung zieht weich nach: Verfolger zuegig, Vogel ruhig, Heli traege.
        const double kKurs = art == QLatin1String("heli") ? 0.4 : art == QLatin1String("vogel") ? 1.0 : 1.5;
        if (schnitt)
            m_kamKurs = hinten->kursIst;
        else
            m_kamKurs += winkelDiff(hinten->kursIst, m_kamKurs) * (1.0 - std::exp(-dt * kKurs));
    }
    const QVector3D vor = vorwaerts(m_kamKurs);
    const QVector3D rechts(-vor.z(), 0, vor.x());

    if (amAuto) {
        // oben erledigt
    } else if (art == QLatin1String("vogel")) {
        // Fast senkrecht von oben (ganz senkrecht kippt lookAt), Fahrtrichtung = Bild-oben.
        const float h = std::max(45.f, (spanne + 20.f) * 1.5f);
        m_augeSoll = ziel - vor * (h * 0.18f) + oben * h;
        m_ziel3Soll = ziel + vor * 2.f;
        m_fovSoll = 40.f;
    } else if (art == QLatin1String("verfolger")) {
        m_augeSoll = ziel - vor * 40.f + oben * 30.f;
        m_ziel3Soll = ziel + vor * 8.f;
        m_fovSoll = 40.f;
    } else if (art == QLatin1String("heli")) {
        // Wie ein Hubschrauber: hoch, seitlich, langes Tele - fliegt weich hinterher (unten).
        m_augeSoll = ziel - vor * 70.f + rechts * 80.f + oben * 110.f;
        m_ziel3Soll = ziel;
        const float abstand = (m_augeSoll - m_ziel3Soll).length();
        m_fovSoll = std::clamp(float(2.0 * std::atan((spanne + 28.f) * 0.6f / abstand) * 180.0 / PI), 6.f, 40.f);
    } else {   // tv
        const double L = m_streckeM > 0 ? m_streckeM : 5000.0;
        auto voraus = [&](const VcTvCam& c) {
            double d = c.frac - std::max(0.0, vorn->frac);
            d -= std::floor(d);
            return d * L;
        };
        const bool vorbei = m_tvAktiv >= 0 && m_tvAktiv < m_tv.size()
                            && (voraus(m_tv[m_tvAktiv]) > L - 80 || voraus(m_tv[m_tvAktiv]) > 700);   // erst 80 m danach schneiden
        if (!m_tv.isEmpty() && (m_tvAktiv < 0 || m_tvAktiv >= m_tv.size() || schnitt || vorbei)) {
            int beste = 0;
            double besteD = 1e18;
            for (int i = 0; i < m_tv.size(); ++i) {
                const double d = voraus(m_tv[i]);
                const double wert = d >= 60 ? d : d + L;          // lieber eine Kamera VOR dem Auto
                if (wert < besteD) {
                    besteD = wert;
                    beste = i;
                }
            }
            if (beste != m_tvAktiv)
                schnitt = true;
            m_tvAktiv = beste;
        }
        if (m_tv.isEmpty()) {
            m_augeSoll = ziel - vor * 60.f + oben * 48.f;
            m_ziel3Soll = ziel;
            m_fovSoll = 40.f;
        } else {
            m_augeSoll = m_tv[m_tvAktiv].p;
            m_ziel3Soll = ziel + oben * 1.f;
            const float abstand = std::max(1.f, (m_augeSoll - m_ziel3Soll).length());
            m_fovSoll = std::clamp(float(2.0 * std::atan(std::max(16.f, spanne + 14.f) * 0.6f / abstand) * 180.0 / PI),
                                   5.f, 45.f);
        }
    }

    if (schnitt) {
        m_auge = m_augeSoll;
        m_ziel3 = m_ziel3Soll;
        m_fov = m_fovSoll;
        return;
    }
    // TV-Kamera steht fest und schwenkt leicht verzoegert, der Heli fliegt hinterher,
    // die mitfahrenden haengen am (schon geglaetteten) Auto.
    const float kAuge = art == QLatin1String("heli") ? 2.5f : 1000.f;
    const float kZiel = art == QLatin1String("tv") ? 10.f : 1000.f;
    m_auge += (m_augeSoll - m_auge) * float(1.0 - std::exp(-dt * kAuge));
    m_ziel3 += (m_ziel3Soll - m_ziel3) * float(1.0 - std::exp(-dt * kZiel));
    m_fov += (m_fovSoll - m_fov) * float(1.0 - std::exp(-dt * 4.0));
}

void VirtualCam::tick()
{
    const double now = double(m_uhr.elapsed());
    const double dt = std::clamp((now - m_letzterTick) / 1000.0, 0.0, 0.2);
    m_letzterTick = now;

    // Strecke neu bauen bei neuer Kontur oder deutlich mehr Hoehenprofil
    // (hoechstens alle 2 s; die Autos stehen immer auf dem gebauten Band)
    if (m_track && (m_track->version() != m_konturVer
                    || (m_hBekannt - m_hGebaut >= 12 && now - m_gebautMs > 2000)))
        baueStrecke();

    // Autos glaetten (bei Spruengen > 60 m: Rueckblende/Box -> sofort hin)
    const double k = 1.0 - std::exp(-dt * 12.0);
    for (Zustand& z : m_zustand) {
        QVector3D soll = z.soll;
        if (z.frac >= 0)
            soll.setY(float(hoehe(z.frac)));        // auf dem Band stehen, nicht darin oder darueber
        if ((soll - z.ist).length() > 60.f)
            z.ist = soll;
        else
            z.ist += (soll - z.ist) * float(k);
        z.kursIst += winkelDiff(z.kursSoll, z.kursIst) * k;
        if (z.frac >= 0 && m_streckeM > 0) {
            const double d = 10.0 / m_streckeM;                                  // +-10 m
            const double soll = std::atan2(hoehe(z.frac + d) - hoehe(z.frac - d + 1.0), 20.0);
            z.neigung += (soll - z.neigung) * k;
        }
    }

    // Kamera - Schnitt bei neuem Motiv oder neuer Kameraart
    // Schnitt nur bei anderen Autos oder anderer Kamera - Kampf -> Ueberholen
    // derselben zwei Autos bleibt eine Einstellung.
    QList<int> autos = m_motiv.autos;
    std::sort(autos.begin(), autos.end());
    const bool schnitt = autos != m_kameraAutos || m_kameraArt != m_kameraFuer;
    m_kameraAutos = autos;
    m_kameraFuer = m_kameraArt;
    kameraRechnen(dt, schnitt);

    // Bild
    VcFrame f;
    f.view.lookAt(m_auge, m_ziel3, QVector3D(0, 1, 0));
    f.fovY = m_fov;
    const float abstand = (m_auge - m_ziel3).length();
    // Am Auto: Halo und Helm liegen Zentimeter vor der Linse.
    const bool onboard = m_kameraArt == QLatin1String("onboard"), heck = m_kameraArt == QLatin1String("heck");
    f.nearZ = (onboard || heck) ? 0.05f : std::clamp(abstand * 0.05f, 0.25f, 200.f);
    f.farZ = std::max(6000.f, abstand * 4.f);
    f.track = m_streckenNetz;
    f.car = m_autoNetz;
    // Das Kamera-Auto bekommt kein Schild (es steht hinter/unter der Linse).
    const int eigenes = m_motiv.autos.isEmpty() ? -1
                        : onboard              ? m_motiv.autos.last()
                        : heck                 ? m_motiv.autos.first()
                                               : -1;
    for (auto it = m_zustand.cbegin(); it != m_zustand.cend(); ++it) {
        VcInstance in;
        in.model = modell(*it);
        in.tint = it->farbe;
        f.cars.append(in);
    }
    m_frame = f;

    // Namensschilder: Motiv-Autos + bis zu drei nahe
    QMatrix4x4 proj;
    proj.perspective(m_fov, 16.f / 9.f, f.nearZ, f.farZ);
    const QMatrix4x4 vp = proj * f.view;
    QList<QPair<float, int>> nah;
    for (auto it = m_zustand.cbegin(); it != m_zustand.cend(); ++it)
        if (!m_motiv.autos.contains(it.key()) && it.key() != eigenes)
            nah.append({(it->ist - m_ziel3).length(), it.key()});
    std::sort(nah.begin(), nah.end());
    QList<int> beschriften;
    for (int idx : m_motiv.autos)
        if (idx != eigenes)
            beschriften.append(idx);
    for (int i = 0; i < nah.size() && i < 3 && nah[i].first < 150.f; ++i)
        beschriften.append(nah[i].second);
    QVariantList schilder;
    for (int idx : beschriften) {
        if (schilder.size() >= 6)          // so viele Plaetze hat VirtualCam.qml
            break;
        const VcCar* c = finde(m_autos, idx);
        const auto z = m_zustand.constFind(idx);
        if (!c || z == m_zustand.cend())
            continue;
        // Von oben laege "ueber dem Auto" auf dem Auto - dort das Schild vor die Nase.
        const QVector3D anker = m_kameraArt == QLatin1String("vogel") ? z->ist + vorwaerts(z->kursIst) * 4.f
                                                                      : z->ist + QVector3D(0, 2.2f, 0);
        const QVector4D clip = vp * QVector4D(anker, 1.f);
        if (clip.w() <= 0.1f)
            continue;
        const float x = (clip.x() / clip.w() + 1.f) / 2.f;
        const float y = (1.f - clip.y() / clip.w()) / 2.f;
        if (x < -0.05f || x > 1.05f || y < -0.05f || y > 1.05f)
            continue;
        QVariantMap s;
        s.insert("x", x);
        s.insert("y", y);
        s.insert("text", QStringLiteral("P%1 %2").arg(c->pos).arg(c->name.toUpper()));
        s.insert("farbe", QColor::fromRgbF(z->farbe.x(), z->farbe.y(), z->farbe.z()));
        s.insert("haupt", m_motiv.autos.contains(idx));
        schilder.append(s);
    }
    m_schilder = schilder;
    emit bildChanged();
}

}  // namespace kers::hud
