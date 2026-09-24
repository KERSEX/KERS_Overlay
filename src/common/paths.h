// Wo was liegt.
//
// Zwei Orte, dieselbe Regel wie in der Python-Fassung (common.py, main.py):
//
//   dataDir()    ALLES, was dem Nutzer gehoert: hud_state.json, overlay_settings.json,
//                presets.json, layouts.json, tracks.json, championship.json,
//                recordings\, kers.log. Der Unterordner "data" neben der EXE, damit
//                im Verteilordner nur KERS_Subsystems.exe steht. Rueckfall auf den
//                EXE-Ordner, wenn "data" nicht anlegbar ist (schreibgeschuetzt).
//
//   assetRoot()  Alles Mitgelieferte: static\ (Bilder, Schriften, tracks.json,
//                version.txt) und templates\. Steckt per qrc IN der EXE, deshalb
//                "qrc:/static" bzw. ":/static" - es gibt keinen Entpack-Ordner mehr
//                (in der Python-EXE war das sys._MEIPASS).
#pragma once

#include <QString>
#include <QStringList>

namespace kers::paths {

/// Ordner der laufenden EXE.
QString exeDir();

/// Nutzerdaten: <exeDir>\data (wird angelegt), sonst exeDir.
QString dataDir();

/// Datei im Datenordner, z.B. dataFile("hud_state.json").
QString dataFile(const QString& name);

/// Wurzel der eingebetteten Assets als QML-taugliche URL: "qrc:/static".
QString assetRootUrl();

/// Wurzel der eingebetteten Assets fuer QFile/QDir: ":/static".
QString assetRootPath();

/// Version aus static/version.txt (eingebettet), z.B. "0.3.0".
QString appVersion();

/// Alte Dateien aus dem EXE-Ordner nach data\ holen (Fassungen vor 0.1.1 legten
/// alles neben die EXE). MUSS vor dem ersten Laden von hud_state.json laufen.
/// Was in data\ schon liegt, bleibt unangetastet. Rueckgabe: verschobene Namen.
QStringList migrateData();

}  // namespace kers::paths
