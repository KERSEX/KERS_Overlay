#include "designs.h"

#include "jsonfile.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace kers::designs {

namespace {

const QStringList SZENEN_DATEIEN = {QStringLiteral("*.qml"), QStringLiteral("*.js")};

Info lies(const QString& dir, const QString& id, bool builtin)
{
    Info d;
    d.id = id;
    d.dir = dir;
    d.builtin = builtin;
    const QJsonObject o = json::readObject(QDir(dir).filePath(QStringLiteral("design.json")));
    d.name = o.value("name").toString().trimmed();
    if (d.name.isEmpty())
        d.name = id;
    d.description = o.value("description").toString();
    d.author = o.value("author").toString();
    d.hasQml = QDir(QDir(dir).filePath(QStringLiteral("qml"))).exists();
    return d;
}

/// Alle Szenen-Dateien unter src (rekursiv) nach dst kopieren, bestehende ersetzen.
/// Nur .qml/.js - die qmldir des eingebauten Moduls bleibt draussen, sonst haelt
/// sich der Ordner fuer das Modul "KersHud".
int kopiereSzene(const QString& src, const QString& dst, const QStringList& auslassen = {})
{
    int n = 0;
    QDirIterator it(src, SZENEN_DATEIEN, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(src).relativeFilePath(f);
        if (auslassen.contains(rel))
            continue;
        const QString ziel = QDir(dst).filePath(rel);
        QDir().mkpath(QFileInfo(ziel).absolutePath());
        QFile::remove(ziel);
        if (QFile::copy(f, ziel)) {
            // Aus qrc kopierte Dateien sind schreibgeschuetzt - fuers Weiterbauen freigeben.
            QFile::setPermissions(ziel, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
            ++n;
        }
    }
    return n;
}

/// Ganzen Ordner kopieren (Schriften, Bilder).
void kopiereOrdner(const QString& src, const QString& dst)
{
    if (!QDir(src).exists())
        return;
    QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString ziel = QDir(dst).filePath(QDir(src).relativeFilePath(f));
        QDir().mkpath(QFileInfo(ziel).absolutePath());
        QFile::remove(ziel);
        if (QFile::copy(f, ziel))
            QFile::setPermissions(ziel, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }
}

}  // namespace

QString userRoot(const QString& dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("designs"));
}

QList<Info> list(const QString& dataDir)
{
    QList<Info> out;
    const QDir eingebaut{QString::fromLatin1(BUILTIN_ROOT)};
    for (const QString& id : eingebaut.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        out.append(lies(eingebaut.filePath(id), id, true));
    // Standard-Design immer zuerst
    std::stable_sort(out.begin(), out.end(), [](const Info& a, const Info& b) {
        return (a.id == QLatin1String(DEFAULT_ID)) > (b.id == QLatin1String(DEFAULT_ID));
    });
    const QDir eigen(userRoot(dataDir));
    for (const QString& id : eigen.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (id.startsWith(QLatin1Char('.')) || !QFileInfo::exists(eigen.filePath(id + QStringLiteral("/design.json"))))
            continue;
        const Info d = lies(eigen.filePath(id), id, false);
        bool ersetzt = false;
        for (Info& b : out) {
            if (b.id.compare(id, Qt::CaseInsensitive) == 0) {
                b = d;
                ersetzt = true;
            }
        }
        if (!ersetzt)
            out.append(d);
    }
    return out;
}

Info find(const QString& dataDir, const QString& id)
{
    const QList<Info> alle = list(dataDir);
    for (const Info& d : alle)
        if (!id.isEmpty() && d.id.compare(id, Qt::CaseInsensitive) == 0)
            return d;
    for (const Info& d : alle)
        if (d.id == QLatin1String(DEFAULT_ID))
            return d;
    // Ohne eingebaute Designs (Tests): ein leeres Standard-Design
    Info leer;
    leer.id = QLatin1String(DEFAULT_ID);
    leer.name = QStringLiteral("KERS");
    leer.builtin = true;
    return leer;
}

bool exists(const QString& dataDir, const QString& id)
{
    for (const Info& d : list(dataDir))
        if (d.id.compare(id, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QJsonObject readTokens(const Info& d)
{
    if (d.dir.isEmpty())
        return {};
    return json::readObject(QDir(d.dir).filePath(QStringLiteral("design.json")));
}

QString prepareScene(const QString& dataDir, const Info& d, QString* error, const QString& slot)
{
    if (!d.hasQml)
        return QString();
    const QString lauf = QDir(userRoot(dataDir))
                             .filePath(QStringLiteral(".laufzeit/") + d.id + (slot.isEmpty() ? QString() : QLatin1Char('-') + slot));
    QDir(lauf).removeRecursively();
    if (!QDir().mkpath(lauf)) {
        if (error)
            *error = QStringLiteral("Laufzeit-Ordner nicht anlegbar: %1").arg(lauf);
        return QString();
    }
    const int basis = kopiereSzene(QLatin1String(SCENE_ROOT), lauf, {QStringLiteral("M0Probe.qml")});
    const int eigen = kopiereSzene(QDir(d.dir).filePath(QStringLiteral("qml")), lauf);
    if (!QFileInfo::exists(QDir(lauf).filePath(QStringLiteral("Overlay.qml")))) {
        if (error)
            *error = QStringLiteral("Overlay.qml fehlt im zusammengesetzten Design (%1 eingebaut, %2 eigen)").arg(basis).arg(eigen);
        return QString();
    }
    qInfo().noquote() << "[DESIGN]" << d.name << "-" << eigen << "eigene Szenen-Dateien," << basis << "eingebaut";
    return lauf;
}

bool validName(const QString& name)
{
    static const QRegularExpression re(QStringLiteral("^[\\p{L}\\p{N}_][\\p{L}\\p{N} _.\\-]{0,39}$"));
    return re.match(name).hasMatch() && !name.endsWith(QLatin1Char('.')) && !name.endsWith(QLatin1Char(' '));
}

QString exportDesign(const QString& dataDir, const QString& fromId, const QString& newName, QString* error)
{
    auto fehler = [error](const QString& t) {
        if (error)
            *error = t;
        return QString();
    };
    const QString name = newName.trimmed();
    if (!validName(name))
        return fehler(QStringLiteral("Name ungueltig - erlaubt sind Buchstaben, Ziffern, Leerzeichen, _ - . (max. 40)"));
    const Info von = find(dataDir, fromId);
    const QString ziel = QDir(userRoot(dataDir)).filePath(name);
    if (QFileInfo::exists(ziel))
        return fehler(QStringLiteral("Den Ordner \"%1\" gibt es schon").arg(name));
    if (!QDir().mkpath(ziel))
        return fehler(QStringLiteral("Ordner nicht anlegbar: %1").arg(ziel));

    // design.json mit neuem Namen
    QJsonObject tokens = readTokens(von);
    tokens["name"] = name;
    tokens["based_on"] = von.name;
    json::writeObject(QDir(ziel).filePath(QStringLiteral("design.json")), tokens);
    // Die GANZE Szene: eingebaut, darueber die Dateien des Ausgangs-Designs
    const QString qml = QDir(ziel).filePath(QStringLiteral("qml"));
    kopiereSzene(QLatin1String(SCENE_ROOT), qml, {QStringLiteral("M0Probe.qml")});
    if (von.hasQml)
        kopiereSzene(QDir(von.dir).filePath(QStringLiteral("qml")), qml);
    kopiereOrdner(QDir(von.dir).filePath(QStringLiteral("fonts")), QDir(ziel).filePath(QStringLiteral("fonts")));
    kopiereOrdner(QDir(von.dir).filePath(QStringLiteral("images")), QDir(ziel).filePath(QStringLiteral("images")));
    const QString readme = QStringLiteral("%1/README.txt").arg(QLatin1String(BUILTIN_ROOT));
    if (QFileInfo::exists(readme)) {
        QFile::copy(readme, QDir(ziel).filePath(QStringLiteral("README.txt")));
        QFile::setPermissions(QDir(ziel).filePath(QStringLiteral("README.txt")),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }
    qInfo().noquote() << "[DESIGN]" << von.name << "kopiert nach" << ziel;
    return name;
}

}  // namespace kers::designs
