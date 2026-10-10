// Designs (ab 0.3.1): wie das Desktop-HUD aussieht.
//
// Ein Design ist ein Ordner:
//   design.json   Name, Farben, Deckkraft, Formen, Schriften (siehe README.txt)
//   fonts\        eigene .ttf/.otf (optional)
//   qml\          Ersatz fuer einzelne Dateien der Szene, gleiche Pfade wie im
//                 eingebauten Design, z. B. qml\parts\Tower.qml (optional)
//
// Eingebaut (in der EXE): :/designs/<id>. Eigene: data\designs\<Ordner>. Ein
// eigener Ordner mit demselben Namen wie ein eingebautes Design gewinnt.
//
// Hat ein Design eigene QML-Dateien, wird die Szene vor dem Laden in einem
// Laufzeit-Ordner zusammengesetzt (data\designs\.laufzeit\<id>): erst die
// eingebaute Szene, darueber die Dateien des Designs. Geladen wird dann von dort -
// relative Importe ("parts", "..") funktionieren dadurch ohne Umwege, und "Neu
// laden" im Schaltbrett zeigt Aenderungen sofort.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace kers::designs {

inline constexpr const char* BUILTIN_ROOT = ":/designs";
inline constexpr const char* SCENE_ROOT = ":/KersHud";     // die eingebaute Szene (qt_add_qml_module)
inline constexpr const char* DEFAULT_ID = "kers";

struct Info {
    QString id;            // Ordnername
    QString name;          // aus design.json, sonst der Ordnername
    QString description;
    QString author;
    QString dir;           // ":/designs/f1tv" oder ein Ordner auf der Platte
    bool builtin = false;
    bool hasQml = false;   // eigene Szenen-Dateien?
};

QString userRoot(const QString& dataDir);
/// Alle Designs: eingebaute zuerst, dann eigene (ein eigenes ersetzt ein gleichnamiges eingebautes).
QList<Info> list(const QString& dataDir);
/// "" oder unbekannt -> das eingebaute Standard-Design.
Info find(const QString& dataDir, const QString& id);
bool exists(const QString& dataDir, const QString& id);
QJsonObject readTokens(const Info& d);
/// Szene bereitstellen. Leer = eingebaute Szene laden (qrc:/KersHud/Overlay.qml),
/// sonst der Ordner, in dem Overlay.qml liegt. slot trennt mehrere Fenster
/// (HUD und OBS-Fenster setzen je einen eigenen Ordner zusammen).
QString prepareScene(const QString& dataDir, const Info& d, QString* error = nullptr, const QString& slot = QString());
/// Ordnername erlaubt? Buchstaben, Ziffern, Leerzeichen, _ - . (nicht vorn), max. 40.
bool validName(const QString& name);
/// Ein Design komplett als eigenes kopieren - mit ALLEN Szenen-Dateien, damit man
/// von dort aus alles umbauen kann. Rueckgabe: neue id, leer bei Fehler.
QString exportDesign(const QString& dataDir, const QString& fromId, const QString& newName, QString* error = nullptr);

}  // namespace kers::designs
