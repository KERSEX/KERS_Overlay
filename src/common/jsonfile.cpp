#include "jsonfile.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace kers::json {

QJsonObject readObject(const QString& path, bool* ok)
{
    if (ok)
        *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    if (ok)
        *ok = true;
    return doc.object();
}

bool writeObject(const QString& path, const QJsonObject& obj, bool indented)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const QJsonDocument doc(obj);
    f.write(doc.toJson(indented ? QJsonDocument::Indented : QJsonDocument::Compact));
    return f.commit();
}

}  // namespace kers::json
