// VirtualCam (ab 0.3.3): der kleine eigene 3D-Zeichner fuer das Bild-im-Bild.
//
// Bewusst KEIN Qt Quick 3D - das gibt es nur unter GPLv3, KERS ist MIT und die
// EXE soll schlank bleiben. QQuickRhiItem (Qt Quick, LGPL) rendert ueber QRhi
// (hier D3D11) in eine Textur, die Qt Quick wie jedes andere Item zeichnet.
//
// Aufteilung (Pflicht bei QQuickRhiItem): das Item lebt im GUI-Thread und haelt
// nur das fertige Bild der Szene (VcFrame). Der Renderer lebt im Render-Thread
// und bekommt eine Kopie in synchronize(), waehrend der GUI-Thread wartet.
//
// Die Szene: ein Netz fuer die Strecke (eine Instanz, Einheitsmatrix) und ein
// Netz fuer EIN Auto, das je Auto einmal instanziert gezeichnet wird (Matrix +
// Teamfarbe) - ein Draw-Call fuer das ganze Feld.
#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QQuickRhiItem>
#include <QVector4D>
#include <QVector>

#include <memory>

namespace kers::hud {

/// Ein Netz: je Ecke Position (3), Normale (3), Farbe (4). Farbe.a = 1 heisst
/// "Teamfarbe der Instanz nehmen", 0 = die Eckfarbe bleibt (Reifen, Fluegel).
struct VcMesh {
    static constexpr int FLOATS = 10;
    QVector<float> vertices;
    int count() const { return int(vertices.size() / FLOATS); }
};

struct VcInstance {
    QMatrix4x4 model;
    QVector4D tint{1, 1, 1, 1};
};

/// Alles, was fuer ein Bild noetig ist. Netze sind geteilt (shared_ptr), damit
/// die Kopie je Bild nichts kostet; ein neues Netz = ein neuer Zeiger.
struct VcFrame {
    QMatrix4x4 view;
    float fovY = 45.f;
    float nearZ = 0.5f;
    float farZ = 8000.f;
    QColor clear{12, 14, 19};
    std::shared_ptr<const VcMesh> track;
    std::shared_ptr<const VcMesh> car;
    QVector<VcInstance> cars;
};

/// Das Item fuer QML (`VirtualCamView`). Die Szene kommt von aussen per setFrame().
class VirtualCam;

class VirtualCamView : public QQuickRhiItem {
    Q_OBJECT
    /// Die Steuerung (`Kers.vcam`) - jedes neue Bild von dort wird gezeichnet.
    Q_PROPERTY(QObject* quelle READ quelle WRITE setQuelle NOTIFY quelleChanged)
public:
    explicit VirtualCamView(QQuickItem* parent = nullptr);

    /// Neues Bild uebernehmen und neu zeichnen lassen (GUI-Thread).
    void setFrame(const VcFrame& f);
    const VcFrame& frame() const { return m_frame; }
    QObject* quelle() const;
    void setQuelle(QObject* q);

signals:
    void quelleChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    VcFrame m_frame;
    VirtualCam* m_quelle = nullptr;
};

/// Einmal vor dem ersten Laden einer Szene: `import KersCam` -> VirtualCamView.
void registerVcamTypes();

}  // namespace kers::hud
