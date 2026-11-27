// VirtualCam (ab 0.3.3): die Steuerung hinter dem Bild-im-Bild.
//
// Je Payload kommen Fahrer, Fokus und Regie herein (update). Die eigene Uhr
// (tick, Takt = Einstellung vcam_fps) glaettet die Autos, waehlt mit der
// Auto-Regie das Motiv, rechnet die Kamera und baut daraus das Bild (VcFrame)
// fuer den Renderer - geteilt von HUD- und OBS-Fenster.
//
// Koordinaten wie im Spiel: rechtshaendig, y oben, Meter (an 13 echten
// Streckenkonturen geprueft - nichts wird gespiegelt).
#pragma once

#include "derive.h"
#include "kers_prop.h"
#include "vcam_item.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector3D>

namespace kers::hud {

class Trackmap;

/// Ein Auto aus Sicht der Regie.
struct VcCar {
    int idx = -1;
    int pos = 0;
    QString name;
    QVector3D p;            // Weltposition
    double lapDist = 0;     // m in der Runde
    double gapAhead = 0;    // s zum Vordermann
    bool inPit = false;
    bool hotlap = false;    // Quali: auf schneller Runde
};

/// Was gezeigt wird.
struct VcMotiv {
    enum Art { Keins, Fahrer, Fuehrender, Kampf, Ueberholen, Box, Hotlap };   // Keins = Bild aus
    Art art = Keins;
    QList<int> autos;       // Fahrer-Indizes, vorn zuerst
    int fuerPlatz = 0;      // Kampf/Ueberholen: um welchen Platz
    /// Dieselben Autos in anderer Reihenfolge sind dasselbe Motiv (Kampf nach einem Platztausch).
    bool operator==(const VcMotiv& o) const
    {
        if (art != o.art || autos.size() != o.autos.size())
            return false;
        for (int a : autos)
            if (!o.autos.contains(a))
                return false;
        return true;
    }
    bool operator!=(const VcMotiv& o) const { return !(*this == o); }
};

/// Die Auto-Regie - reine Logik mit Uhr von aussen, damit testbar.
///
/// HARTE REGEL (Fabian): nie ein Auto zeigen, das die Spielkamera sehen kann.
/// Das Spiel sendet seine Kamera nicht - "sichtbar" heisst deshalb: das
/// Fokus-Auto selbst und alles im Umkreis davon (Strecke oder Luftlinie).
class VcDirector {
public:
    static constexpr double HALT_MIN_MS = 8000;
    static constexpr double HALT_MAX_MS = 25000;
    static constexpr double UEBERHOL_MS = 6000;
    static constexpr double KAMPF_S = 1.0;
    static constexpr double KAMPF_ENG_S = 0.6;

    double sichtStreckeM = 300.0;
    double sichtLuftM = 150.0;
    double freiFaktor = 4.0 / 3.0;   // einmal gesperrt: erst ab 400 m / 200 m wieder frei
    bool kaempfe = true;             // Kampf/Ueberholen erlaubt (kein SC/VSC/Start/rote Flagge, ab Runde 3)

    /// focus = Index des Autos der Spielkamera (-1 = keins). nowMs = Uhr.
    VcMotiv waehle(const QList<VcCar>& autos, int focus, bool quali, double streckeM, double nowMs);
    bool sichtbar(const VcCar& c, const VcCar* fokus, double streckeM, bool warGesperrt = false) const;
    double seit() const { return m_seit; }
    void reset();

private:
    struct Manoever {
        int vorn, hinten, platz;
        double t;
    };
    void merkeUeberholen(const QList<VcCar>& autos, double nowMs, double streckeM);
    QList<QPair<int, VcMotiv>> kandidaten(const QList<VcCar>& autos, const QSet<int>& gesperrt, bool quali,
                                          double nowMs) const;
    bool gueltig(const VcMotiv& m, const QList<VcCar>& autos, const QSet<int>& gesperrt, bool quali,
                 double nowMs) const;

    VcMotiv m_motiv;
    double m_seit = 0;
    QHash<int, int> m_platz;
    QList<Manoever> m_manoever;
    QSet<int> m_gesperrt;
};

/// Kamera bei "Auto": je Motiv eine Folge von Einstellungen, alle HALT_MIN_MS die naechste.
QString vcAutoKamera(VcMotiv::Art art, double gehaltenMs);

/// Feste TV-Kamera an der Strecke.
struct VcTvCam {
    QVector3D p;
    double frac = 0;
};

/// TV-Kameras entlang der Kontur: alle `abstandM` eine, auf der Kurvenaussenseite.
QList<VcTvCam> vcTvKameras(const QList<QVector3D>& kontur, const QList<double>& fracs, double abstandM);

/// Die Steuerung fuer QML (`Kers.vcam`).
class VirtualCam : public QObject {
    Q_OBJECT
    KERS_PROP(bool, isVisible, false)
    KERS_PROP(QString, titel, QString())
    KERS_PROP(QString, kamera, QString())
    Q_PROPERTY(QVariantList schilder READ schilder NOTIFY bildChanged)
public:
    VirtualCam(SharedState& shared, Config& cfg, Trackmap* track, QObject* parent = nullptr);

    /// Je Payload. kameraWunsch: auto|vogel|heli|verfolger|onboard|heck|tv; ziel = Fahrer-Index, -1 = Auto-Regie.
    void update(const QList<QJsonObject>& drivers, const QJsonObject& session, int focus, bool connected,
                bool an, const QString& kameraWunsch, int ziel);

    const VcFrame& frame() const { return m_frame; }
    QVariantList schilder() const { return m_schilder; }
    VcDirector& regie() { return m_regie; }

signals:
    void bildChanged();

private:
    struct Zustand {
        QVector3D soll, ist;
        double kursSoll = 0, kursIst = 0;
        bool neu = true;
        QVector3D letzte;           // fuer den Kurs aus der Bewegung
        QVector4D farbe;
        double frac = -1;
        double neigung = 0;         // Steigung aus dem Hoehenprofil (rad, Nase hoch > 0)
    };
    static QMatrix4x4 modell(const Zustand& z);
    void tick();
    void baueStrecke();
    void profilRechnen();
    double hoehe(double frac) const;
    void kameraRechnen(double dt, bool schnitt);

    SharedState& m_shared;
    Config& m_cfg;
    Trackmap* m_track;
    QTimer m_timer;
    QElapsedTimer m_uhr;
    double m_letzterTick = 0;

    QList<VcCar> m_autos;
    QHash<int, Zustand> m_zustand;
    int m_fokus = -1;
    bool m_quali = false;
    double m_streckeM = 0;
    QString m_kameraWunsch = QStringLiteral("auto");
    int m_ziel = -1;
    VcDirector m_regie;
    VcMotiv m_motiv;
    QList<int> m_motivAutos;        // sortiert - neue Autos = Kamerafolge von vorn
    double m_motivSeit = 0;
    QString m_kameraArt;

    // Hoehenprofil (Rundenanteil-Bins) + Streckennetz
    static constexpr int BINS = 600;
    QVector<double> m_hSumme, m_hAnzahl;
    int m_hBekannt = 0, m_hGebaut = -1;
    double m_gebautMs = -1e9;
    QVector<double> m_bandH;        // Hoehe je Bin, so wie das Band gebaut ist (Luecken interpoliert)
    int m_konturVer = -2;
    QList<QVector3D> m_kontur;      // x, (Hoehe), z
    QList<double> m_konturFrac;
    QList<VcTvCam> m_tv;
    int m_tvAktiv = -1;
    std::shared_ptr<const VcMesh> m_streckenNetz;
    std::shared_ptr<const VcMesh> m_autoNetz;

    // Kamera
    QVector3D m_auge, m_ziel3, m_augeSoll, m_ziel3Soll;
    float m_fov = 45.f, m_fovSoll = 45.f;
    double m_kamKurs = 0;           // Blickrichtung der mitfahrenden Kameras (weich)
    QList<int> m_kameraAutos;       // sortiert - fuer "Schnitt?"
    QString m_kameraFuer;

    VcFrame m_frame;
    QVariantList m_schilder;
};

}  // namespace kers::hud
