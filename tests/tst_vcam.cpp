// VirtualCam (ab 0.3.3): Auto-Regie und TV-Kameras.
//
// Fabians harte Regel: die Auto-Regie zeigt nie ein Auto, das man im Spielbild
// sehen kann - Fokus-Auto, seine Kampfgruppe und alles im Umkreis. Die Autos
// stehen hier auf einer Geraden (x = Rundenmeter), damit Luftlinie und
// Streckenabstand dasselbe sind.
#include "vcam.h"

#include <QtTest>

#include <cmath>

using namespace kers::hud;

namespace {

constexpr double L = 5000;

VcCar wagen(int idx, int pos, double lapDist, double gap, bool hotlap = false)
{
    VcCar c;
    c.idx = idx;
    c.pos = pos;
    c.name = QStringLiteral("F%1").arg(idx);
    c.p = QVector3D(float(lapDist), 0, 0);
    c.lapDist = lapDist;
    c.gapAhead = gap;
    c.hotlap = hotlap;
    return c;
}

}  // namespace

class TestVcam : public QObject {
    Q_OBJECT
private slots:
    void zeigtNieDenFokusOderSeinenKampf()
    {
        VcDirector d;
        const QList<VcCar> autos = {
            wagen(1, 1, 4000, 0),     // Fuehrender, weit weg
            wagen(2, 2, 150, 3.0),    // 50 m vor dem Fokus - im Spielbild
            wagen(0, 3, 100, 0.4),    // Fokus, im Kampf mit 2
            wagen(5, 4, 2000, 4.0),
            wagen(6, 5, 1990, 0.8),   // Kampf 5/6 weit weg
        };
        const VcMotiv m = d.waehle(autos, 0, false, L, 1000);
        QCOMPARE(m.art, VcMotiv::Kampf);
        QCOMPARE(m.autos, (QList<int>{5, 6}));
        QCOMPARE(m.fuerPlatz, 4);
    }

    void fokusUnbekanntHeisstBildAus()
    {
        VcDirector d;
        const QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(5, 2, 2000, 0.3)};
        QCOMPARE(d.waehle(autos, 255, false, L, 1000).art, VcMotiv::Keins);
    }

    void autoKameraFolge()
    {
        // Kampf: TV, dann Heckkamera des Vorderen, Verfolger, Onboard - alle 8 s weiter.
        QCOMPARE(vcAutoKamera(VcMotiv::Kampf, 0), QStringLiteral("tv"));
        QCOMPARE(vcAutoKamera(VcMotiv::Kampf, 8500), QStringLiteral("heck"));
        QCOMPARE(vcAutoKamera(VcMotiv::Ueberholen, 17000), QStringLiteral("verfolger"));
        QCOMPARE(vcAutoKamera(VcMotiv::Kampf, 33000), QStringLiteral("tv"));   // von vorn
        QCOMPARE(vcAutoKamera(VcMotiv::Fuehrender, 0), QStringLiteral("heli"));
        QCOMPARE(vcAutoKamera(VcMotiv::Hotlap, 0), QStringLiteral("verfolger"));
    }

    void keinKampfUnterSafetyCar()
    {
        VcDirector d;
        d.kaempfe = false;
        const QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(0, 2, 100, 3.0), wagen(5, 3, 2000, 2.0),
                                    wagen(6, 4, 1990, 0.3)};
        const VcMotiv m = d.waehle(autos, 0, false, L, 1000);
        QCOMPARE(m.art, VcMotiv::Fuehrender);
        QCOMPARE(m.autos, (QList<int>{1}));
    }

    void haltezeitVorBesseremMotiv()
    {
        VcDirector d;
        QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(0, 2, 100, 3.0), wagen(5, 3, 2000, 2.0),
                              wagen(6, 4, 1990, 3.0)};
        QCOMPARE(d.waehle(autos, 0, false, L, 1000).art, VcMotiv::Fuehrender);
        autos[3].gapAhead = 0.3;                                    // jetzt ein enger Kampf
        QCOMPARE(d.waehle(autos, 0, false, L, 4000).art, VcMotiv::Fuehrender);   // erst 3 s
        QCOMPARE(d.waehle(autos, 0, false, L, 9100).art, VcMotiv::Kampf);        // nach 8 s
    }

    void sichtbarWirdSofortWeggeschnitten()
    {
        VcDirector d;
        QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(0, 2, 100, 3.0), wagen(5, 3, 2000, 2.0),
                              wagen(6, 4, 1990, 0.3)};
        QCOMPARE(d.waehle(autos, 0, false, L, 1000).art, VcMotiv::Kampf);
        autos[1].lapDist = 1900;                                    // Fokus faehrt heran
        autos[1].p.setX(1900);
        const VcMotiv m = d.waehle(autos, 0, false, L, 1500);      // 0,5 s - Haltezeit egal
        QVERIFY(!m.autos.contains(5) && !m.autos.contains(6));
    }

    void sperreMitHysterese()
    {
        VcDirector d;
        QList<VcCar> autos = {wagen(7, 1, 290, 0), wagen(0, 2, 0, 1.0)};
        QCOMPARE(d.waehle(autos, 0, false, L, 1000).art, VcMotiv::Keins);        // 290 m: gesperrt
        autos[0].lapDist = 350;
        autos[0].p.setX(350);
        QCOMPARE(d.waehle(autos, 0, false, L, 11000).art, VcMotiv::Keins);       // 350 m: noch gesperrt
        autos[0].lapDist = 410;
        autos[0].p.setX(410);
        QCOMPARE(d.waehle(autos, 0, false, L, 21000).art, VcMotiv::Fuehrender);  // 410 m: frei
    }

    void streckenabstandUeberDieLinie()
    {
        VcDirector d;
        const VcCar fokus = wagen(0, 1, 20, 0);
        VcCar c = wagen(1, 2, 4900, 1.0);
        c.p = QVector3D(5000, 0, 0);                                // Luftlinie weit, Strecke 120 m
        QVERIFY(d.sichtbar(c, &fokus, L));
    }

    void qualiNimmtDieWeitesteSchnelleRunde()
    {
        VcDirector d;
        const QList<VcCar> autos = {wagen(0, 1, 100, 0), wagen(3, 2, 2000, 0, true), wagen(4, 3, 3000, 0, true),
                                    wagen(8, 4, 2500, 0)};
        const VcMotiv m = d.waehle(autos, 0, true, L, 1000);
        QCOMPARE(m.art, VcMotiv::Hotlap);
        QCOMPARE(m.autos, (QList<int>{4}));
    }

    void ueberholenImKampfWechseltNurDieUnterschrift()
    {
        VcDirector d;
        QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(0, 2, 100, 3.0), wagen(5, 3, 2000, 2.0),
                              wagen(6, 4, 1990, 0.3)};
        QCOMPARE(d.waehle(autos, 0, false, L, 1000).art, VcMotiv::Kampf);
        autos[2] = wagen(5, 4, 2005, 0.2);                          // 6 geht an 5 vorbei
        autos[3] = wagen(6, 3, 2010, 2.0);
        const VcMotiv m = d.waehle(autos, 0, false, L, 2000);
        QCOMPARE(m.art, VcMotiv::Ueberholen);
        QCOMPARE(m.autos, (QList<int>{6, 5}));
        QCOMPARE(m.fuerPlatz, 3);
    }

    void platztauschOhneNaeheIstKeinUeberholen()
    {
        VcDirector d;
        QList<VcCar> autos = {wagen(1, 1, 4000, 0), wagen(0, 2, 100, 3.0), wagen(5, 3, 2000, 9.0),
                              wagen(6, 4, 3000, 9.0)};
        d.waehle(autos, 0, false, L, 1000);
        autos[2].pos = 4;                                           // Strafe: 5 faellt hinter 6
        autos[3].pos = 3;
        for (double t = 2000; t < 30000; t += 1000)
            QVERIFY(d.waehle(autos, 0, false, L, t).art != VcMotiv::Ueberholen);
    }

    void tvKamerasAussenUndVerteilt_data()
    {
        QTest::addColumn<bool>("linksherum");
        QTest::newRow("links") << true;
        QTest::newRow("rechts") << false;
    }

    void tvKamerasAussenUndVerteilt()
    {
        // Kreis, r = 300 m, 7 m hoch: aussen heisst weiter weg vom Mittelpunkt.
        QFETCH(bool, linksherum);
        QList<QVector3D> kreis;
        QList<double> fracs;
        const int n = 400;
        for (int i = 0; i < n; ++i) {
            const double a = 2 * M_PI * i / n * (linksherum ? 1 : -1);
            kreis.append(QVector3D(float(300 * std::cos(a)), 7, float(300 * std::sin(a))));
            fracs.append(double(i) / n);
        }
        const QList<VcTvCam> cams = vcTvKameras(kreis, fracs, 260);
        QCOMPARE(cams.size(), 7);                                    // 1885 m / 260 m
        for (const VcTvCam& c : cams) {
            QVERIFY(std::hypot(c.p.x(), c.p.z()) > 320);
            QCOMPARE(c.p.y(), 16.f);                                // 9 m ueber der Strecke
        }
        for (int i = 1; i < cams.size(); ++i)
            QVERIFY(cams[i].frac > cams[i - 1].frac);
    }
};

QTEST_GUILESS_MAIN(TestVcam)
#include "tst_vcam.moc"
