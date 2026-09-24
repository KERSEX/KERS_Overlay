#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace kers::paths {

QString exeDir()
{
    return QCoreApplication::applicationDirPath();
}

QString dataDir()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;
    QDir dir(exeDir());
    if (dir.exists("data") || dir.mkdir("data"))
        cached = dir.filePath("data");
    else
        cached = dir.absolutePath();          // Rueckfall: wie frueher neben die EXE
    return cached;
}

QString dataFile(const QString& name)
{
    return QDir(dataDir()).filePath(name);
}

QString assetRootUrl()
{
    return QStringLiteral("qrc:/static");
}

QString assetRootPath()
{
    return QStringLiteral(":/static");
}

QString appVersion()
{
    QFile f(QStringLiteral(":/static/version.txt"));
    if (f.open(QIODevice::ReadOnly)) {
        const QString v = QString::fromUtf8(f.readAll()).trimmed();
        if (!v.isEmpty())
            return v;
    }
    return QStringLiteral(KERS_VERSION_STR);
}

QStringList migrateData()
{
    // Wie migrate_data() in common.py - nur ohne main.exe: die gibt es nicht mehr.
    QStringList umgezogen;
    const QDir exe(exeDir());
    const QDir data(dataDir());
    if (QDir::cleanPath(exe.absolutePath()) == QDir::cleanPath(data.absolutePath()))
        return umgezogen;                      // Rueckfall-Betrieb ohne data\ - nichts zu tun
    QStringList namen = {QStringLiteral("hud_state.json"), QStringLiteral("overlay_settings.json"),
                         QStringLiteral("presets.json"), QStringLiteral("championship.json"),
                         QStringLiteral("recordings")};
    for (const QString& f : exe.entryList({QStringLiteral("*.json")}, QDir::Files))
        if (!namen.contains(f))
            namen << f;                        // frei benannte WM-Staende, layouts.json, tracks.json
    for (const QString& name : namen) {
        const QString alt = exe.filePath(name), neu = data.filePath(name);
        if (!QFileInfo::exists(alt) || QFileInfo::exists(neu))
            continue;                          // nichts da, oder im Ziel schon vorhanden
        if (QDir().rename(alt, neu))
            umgezogen << name;
    }
    return umgezogen;
}

}  // namespace kers::paths
