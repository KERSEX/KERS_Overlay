#include "vcam_mesh.h"

namespace kers::hud {

namespace {

void ecke(VcMesh& m, QVector3D p, QVector3D n, QColor c, float team)
{
    m.vertices << p.x() << p.y() << p.z() << n.x() << n.y() << n.z()
               << float(c.redF()) << float(c.greenF()) << float(c.blueF()) << team;
}

}  // namespace

void vcAddBox(VcMesh& m, QVector3D mitte, QVector3D groesse, QColor farbe, bool teamfarbe)
{
    const QVector3D h = groesse / 2.f;
    const float team = teamfarbe ? 1.f : 0.f;
    // Teamfarbene Teile tragen Weiss als Eckfarbe - der Shader multipliziert mit der Instanzfarbe.
    const QColor c = teamfarbe ? QColor(Qt::white) : farbe;
    // Je Seite: Normale, dann zwei Achsen in der Flaeche.
    struct Seite {
        QVector3D n, u, v;
    };
    static const Seite seiten[6] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    for (const Seite& s : seiten) {
        const QVector3D z = mitte + s.n * h;
        const QVector3D u = s.u * h;
        const QVector3D v = s.v * h;
        const QVector3D a = z - u - v, b = z + u - v, cc = z + u + v, d = z - u + v;
        for (const QVector3D& p : {a, b, cc, a, cc, d})
            ecke(m, p, s.n, c, team);
    }
}

std::shared_ptr<const VcMesh> vcCarMesh()
{
    auto m = std::make_shared<VcMesh>();
    const QColor dunkel(24, 26, 32);
    const QColor reifen(14, 14, 16);
    vcAddBox(*m, {0, 0.32f, 0}, {1.5f, 0.28f, 4.4f}, {}, true);              // Unterboden/Chassis
    vcAddBox(*m, {0, 0.55f, -0.35f}, {1.45f, 0.42f, 1.9f}, {}, true);        // Seitenkaesten
    vcAddBox(*m, {0, 0.85f, -1.0f}, {0.5f, 0.55f, 1.7f}, {}, true);          // Motorabdeckung/Airbox
    vcAddBox(*m, {0, 0.98f, 0.45f}, {0.42f, 0.12f, 0.95f}, dunkel, false);   // Halo/Cockpit
    vcAddBox(*m, {0, 0.38f, 2.3f}, {0.36f, 0.26f, 1.6f}, {}, true);          // Nase
    vcAddBox(*m, {0, 0.12f, 3.2f}, {1.95f, 0.06f, 0.5f}, dunkel, false);     // Frontfluegel
    vcAddBox(*m, {0, 1.02f, -2.45f}, {1.1f, 0.32f, 0.14f}, {}, true);        // Heckfluegel
    vcAddBox(*m, {0.58f, 0.8f, -2.4f}, {0.04f, 0.5f, 0.5f}, dunkel, false);  // Endplatten
    vcAddBox(*m, {-0.58f, 0.8f, -2.4f}, {0.04f, 0.5f, 0.5f}, dunkel, false);
    for (const float z : {1.75f, -1.6f})                                       // Raeder
        for (const float x : {0.86f, -0.86f})
            vcAddBox(*m, {x, 0.34f, z}, {0.4f, 0.68f, 0.68f}, reifen, false);
    return m;
}

namespace {

/// Ein flaches Viereck (zwei Dreiecke), Normale nach oben.
void viereck(VcMesh& m, QVector3D a, QVector3D b, QVector3D c, QVector3D d, QColor farbe)
{
    for (const QVector3D& p : {a, b, c, a, c, d})
        ecke(m, p, {0, 1, 0}, farbe, 0.f);
}

/// Linke Seite zur Fahrtrichtung t (rechtshaendig, y oben): y x t.
QVector3D links(QVector3D t)
{
    return QVector3D(t.z(), 0, -t.x()).normalized();
}

}  // namespace

std::shared_ptr<const VcMesh> vcStreckenNetz(const QList<QVector3D>& p, int startIdx, float breite)
{
    auto m = std::make_shared<VcMesh>();
    const int n = int(p.size());
    if (n < 3)
        return m;
    const QColor asphalt(48, 52, 60);
    const QColor linie(205, 210, 220);
    const QColor boden(20, 24, 29);
    const float h = breite / 2.f;

    // Boden: unter dem tiefsten Punkt, 6 km Rand um die Strecke
    float minX = p[0].x(), maxX = minX, minZ = p[0].z(), maxZ = minZ, minY = p[0].y();
    for (const QVector3D& q : p) {
        minX = std::min(minX, q.x()); maxX = std::max(maxX, q.x());
        minZ = std::min(minZ, q.z()); maxZ = std::max(maxZ, q.z());
        minY = std::min(minY, q.y());
    }
    const float r = 6000.f, y0 = minY - 0.6f;   // Rand bis hinter den Horizont
    viereck(*m, {minX - r, y0, minZ - r}, {maxX + r, y0, minZ - r}, {maxX + r, y0, maxZ + r},
            {minX - r, y0, maxZ + r}, boden);

    auto tangente = [&](int i) { return (p[(i + 1) % n] - p[(i - 1 + n) % n]).normalized(); };
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const QVector3D li = links(tangente(i)), lj = links(tangente(j));
        // Band
        viereck(*m, p[i] + li * h, p[i] - li * h, p[j] - lj * h, p[j] + lj * h, asphalt);
        // Randlinien (0,5 m breit, knapp ueber dem Band)
        const QVector3D hoch(0, 0.05f, 0);
        for (const float s : {1.f, -1.f}) {
            viereck(*m, p[i] + li * (s * h) + hoch, p[i] + li * (s * (h - 0.5f)) + hoch,
                    p[j] + lj * (s * (h - 0.5f)) + hoch, p[j] + lj * (s * h) + hoch, linie);
        }
    }
    // Start/Ziel: weisser Strich quer, 2 m lang
    const int s = ((startIdx % n) + n) % n;
    const QVector3D t = tangente(s), l = links(t), hoch(0, 0.08f, 0);
    viereck(*m, p[s] + l * h + hoch, p[s] - l * h + hoch, p[s] - l * h + t * 2.f + hoch,
            p[s] + l * h + t * 2.f + hoch, QColor(240, 240, 240));
    return m;
}

}  // namespace kers::hud
