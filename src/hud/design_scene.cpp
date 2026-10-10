#include "design_scene.h"

#include "designs.h"
#include "jsonfile.h"
#include "paths.h"
#include "theme.h"

#include <QDebug>
#include <QDir>

namespace kers::hud {

void DesignScene::apply(Theme* theme, const QString& id)
{
    m_wanted = id;
    const designs::Info d = designs::find(paths::dataDir(), id);
    if (!id.isEmpty() && d.id.compare(id, Qt::CaseInsensitive) != 0)
        qWarning().noquote() << "[DESIGN]" << id << "gibt es nicht - Standard-Design";
    theme->applyDesign(d.id, designs::readTokens(d), d.dir);
    QString fehler;
    m_dir = designs::prepareScene(paths::dataDir(), d, &fehler, m_slot);
    if (!fehler.isEmpty())
        qWarning().noquote() << "[DESIGN]" << fehler << "- eingebaute Szene";
    m_id = d.id;
}

QUrl DesignScene::url(const QString& qmlDir) const
{
    if (!qmlDir.isEmpty())
        return QUrl::fromLocalFile(QDir(qmlDir).filePath(QStringLiteral("Overlay.qml")));
    if (!m_dir.isEmpty())
        return QUrl::fromLocalFile(QDir(m_dir).filePath(QStringLiteral("Overlay.qml")));
    return QUrl(QStringLiteral("qrc:/KersHud/Overlay.qml"));
}

QString DesignScene::initialId()
{
    return json::readObject(paths::dataFile(QStringLiteral("overlay_settings.json"))).value("design").toString();
}

}  // namespace kers::hud
