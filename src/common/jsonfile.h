// JSON-Dateien lesen und schreiben - fuer alles in data\.
//
// Geschrieben wird ueber QSaveFile: erst in eine Nebendatei, dann umbenennen.
// So bleibt beim Absturz mitten im Schreiben die alte Datei heil, statt einer
// halben overlay_settings.json.
#pragma once

#include <QJsonObject>
#include <QString>

namespace kers::json {

/// Liest ein JSON-Objekt. Bei fehlender/kaputter Datei: leeres Objekt, *ok = false.
QJsonObject readObject(const QString& path, bool* ok = nullptr);

/// Schreibt ein JSON-Objekt (eingerueckt, UTF-8, Umlaute unverschluesselt).
bool writeObject(const QString& path, const QJsonObject& obj, bool indented = true);

}  // namespace kers::json
