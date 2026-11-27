#include "vcam_item.h"

#include "vcam.h"

#include <QFile>
#include <QQuickRhiItemRenderer>
#include <QtQml/qqml.h>
#include <rhi/qrhi.h>

#include <cstring>

namespace kers::hud {

namespace {

constexpr int INST_FLOATS = 20;          // Matrix (16) + Farbe (4)
constexpr int UBUF_SIZE = 64 + 16 + 16;  // viewProj, Lichtrichtung, Licht (Umgebung, Diffus)

QShader ladeShader(const QString& name)
{
    QFile f(name);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

/// Ein Netz auf der GPU: Eckpuffer + Instanzpuffer. Neu hochgeladen nur, wenn
/// sich der Zeiger aendert (neues Netz) bzw. die Instanzen jedes Bild.
struct GpuMesh {
    std::shared_ptr<const VcMesh> quelle;
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    int instKapazitaet = 0;
    int instanzen = 0;

    void setze(QRhi* rhi, QRhiResourceUpdateBatch* u, const std::shared_ptr<const VcMesh>& m,
               const QVector<VcInstance>& inst)
    {
        if (m != quelle) {
            quelle = m;
            vbuf.reset();
            if (m && m->count() > 0) {
                const quint32 bytes = quint32(m->vertices.size() * sizeof(float));
                vbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
                vbuf->create();
                u->uploadStaticBuffer(vbuf.get(), m->vertices.constData());
            }
        }
        instanzen = int(inst.size());
        if (instanzen == 0)
            return;
        if (instanzen > instKapazitaet) {
            instKapazitaet = qMax(instanzen, 32);
            ibuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                      quint32(instKapazitaet * INST_FLOATS * sizeof(float))));
            ibuf->create();
        }
        QVector<float> daten(instanzen * INST_FLOATS);
        float* p = daten.data();
        for (const VcInstance& i : inst) {
            std::memcpy(p, i.model.constData(), 16 * sizeof(float));   // spaltenweise, wie GLSL mat4
            p[16] = i.tint.x();
            p[17] = i.tint.y();
            p[18] = i.tint.z();
            p[19] = i.tint.w();
            p += INST_FLOATS;
        }
        u->updateDynamicBuffer(ibuf.get(), 0, quint32(daten.size() * sizeof(float)), daten.constData());
    }

    void zeichne(QRhiCommandBuffer* cb) const
    {
        if (!vbuf || !ibuf || instanzen == 0)
            return;
        const QRhiCommandBuffer::VertexInput eingaben[] = {{vbuf.get(), 0}, {ibuf.get(), 0}};
        cb->setVertexInput(0, 2, eingaben);
        cb->draw(quint32(quelle->count()), quint32(instanzen));
    }
};

class VcRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    QRhi* m_rhi = nullptr;
    int m_samples = 0;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    GpuMesh m_track;
    GpuMesh m_car;
    VcFrame m_frame;
};

void VcRenderer::initialize(QRhiCommandBuffer*)
{
    // Neues QRhi (Fenster neu) oder andere Abtastung -> Pipeline neu bauen.
    if (m_rhi != rhi() || m_samples != renderTarget()->sampleCount()) {
        m_pipeline.reset();
        m_track = {};
        m_car = {};
        m_rhi = rhi();
        m_samples = renderTarget()->sampleCount();
    }
    if (m_pipeline)
        return;

    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UBUF_SIZE));
    m_ubuf->create();
    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get())});
    m_srb->create();

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, ladeShader(QStringLiteral(":/vcam/vcam.vert.qsb"))},
                                 {QRhiShaderStage::Fragment, ladeShader(QStringLiteral(":/vcam/vcam.frag.qsb"))}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{VcMesh::FLOATS * sizeof(float)},
                        {INST_FLOATS * sizeof(float), QRhiVertexInputBinding::PerInstance}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},                    // Position
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},    // Normale
        {0, 2, QRhiVertexInputAttribute::Float4, 6 * sizeof(float)},    // Farbe (a = Teamfarbe)
        {1, 3, QRhiVertexInputAttribute::Float4, 0},                    // Instanz: Matrix-Spalten
        {1, 4, QRhiVertexInputAttribute::Float4, 4 * sizeof(float)},
        {1, 5, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
        {1, 6, QRhiVertexInputAttribute::Float4, 12 * sizeof(float)},
        {1, 7, QRhiVertexInputAttribute::Float4, 16 * sizeof(float)},   // Instanz: Farbe
    });
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setSampleCount(m_samples);
    m_pipeline->setDepthTest(true);
    m_pipeline->setDepthWrite(true);
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);   // Netze sind nicht konsequent gewickelt
    m_pipeline->create();
}

void VcRenderer::synchronize(QQuickRhiItem* item)
{
    m_frame = static_cast<VirtualCamView*>(item)->frame();
}

void VcRenderer::render(QRhiCommandBuffer* cb)
{
    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();

    const QSize px = renderTarget()->pixelSize();
    QMatrix4x4 proj = m_rhi->clipSpaceCorrMatrix();
    proj.perspective(m_frame.fovY, px.height() > 0 ? float(px.width()) / float(px.height()) : 1.f,
                     m_frame.nearZ, m_frame.farZ);
    const QMatrix4x4 viewProj = proj * m_frame.view;
    const float licht[8] = {-0.35f, -1.f, -0.25f, 0.f,   // Richtung (schraeg von oben)
                            0.55f, 0.55f, 0.f, 0.f};     // Umgebung, Diffus
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, viewProj.constData());
    u->updateDynamicBuffer(m_ubuf.get(), 64, 32, licht);

    static const QVector<VcInstance> einheit{VcInstance{}};
    m_track.setze(m_rhi, u, m_frame.track, einheit);
    m_car.setze(m_rhi, u, m_frame.car, m_frame.cars);

    // Vormultipliziertes Alpha (Qt Quick erwartet es) - die Box ist ohnehin deckend.
    const QColor c = m_frame.clear;
    const QColor klar = QColor::fromRgbF(c.redF() * c.alphaF(), c.greenF() * c.alphaF(), c.blueF() * c.alphaF(), c.alphaF());
    cb->beginPass(renderTarget(), klar, {1.0f, 0}, u);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport(QRhiViewport(0, 0, float(px.width()), float(px.height())));
    cb->setShaderResources();
    m_track.zeichne(cb);
    m_car.zeichne(cb);
    cb->endPass();
}

}  // namespace

VirtualCamView::VirtualCamView(QQuickItem* parent)
    : QQuickRhiItem(parent)
{
    setSampleCount(4);
}

QObject* VirtualCamView::quelle() const
{
    return m_quelle;
}

void VirtualCamView::setQuelle(QObject* q)
{
    auto* cam = qobject_cast<VirtualCam*>(q);
    if (cam == m_quelle)
        return;
    if (m_quelle)
        disconnect(m_quelle, nullptr, this, nullptr);
    m_quelle = cam;
    if (cam) {
        connect(cam, &VirtualCam::bildChanged, this, [this, cam] { setFrame(cam->frame()); });
        connect(cam, &QObject::destroyed, this, [this] { m_quelle = nullptr; });
        setFrame(cam->frame());
    }
    emit quelleChanged();
}

void VirtualCamView::setFrame(const VcFrame& f)
{
    m_frame = f;
    update();
}

QQuickRhiItemRenderer* VirtualCamView::createRenderer()
{
    return new VcRenderer;
}

void registerVcamTypes()
{
    qmlRegisterType<VirtualCamView>("KersCam", 1, 0, "VirtualCamView");
}

}  // namespace kers::hud
