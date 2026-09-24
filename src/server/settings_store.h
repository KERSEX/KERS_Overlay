// Overlay-Settings, Presets und Layouts - Portierung der Settings-Teile aus main.py.
//
// Serverseitig gespeichert (data\overlay_settings.json) und live ueber /settings
// aenderbar. Das Overlay bekommt sie mit jedem Payload. Presets und Layouts sind
// bewusst SERVERSEITIG (nicht localStorage), damit PC und Handy dasselbe sehen.
//
// Die Validierung beim POST ist die Sicherheitsgrenze: der Server hoert auf
// 0.0.0.0, und einiges davon landet in Dateinamen (Logo, WM-Datei) oder in
// Tastendruecken auf dem Spiel-PC (camnextkey).
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace kers::server {

/// Ein Roh-Layout aus einem Request in einen sauberen Layout-Block umwandeln.
/// nullopt = gar kein Layout (Aufrufer laesst den alten Stand stehen).
/// Eintraege duerfen UNVOLLSTAENDIG sein ({"z": 55} ohne dx/dy ist gueltig).
std::optional<QJsonObject> layoutPruefen(const QJsonValue& v);

/// Eine erlaubte Tastenkombination daraus machen - oder "" (= nicht gesetzt).
QString camTasteOk(const QString& wert);

class SettingsStore {
public:
    static const QJsonObject& defaults();
    /// Startwerte fuer die Layout-Regler (/api/layout) - NICHT Teil der Defaults.
    static QJsonObject layoutStandard();

    /// dataDir = Ordner fuer overlay_settings.json, presets.json, layouts.json,
    /// die WM-Dateien und tracks.json.
    explicit SettingsStore(const QString& dataDir);

    const QJsonObject& settings() const { return m_settings; }
    /// Ein Patch aus /api/settings anwenden (Whitelist + Validierung) und speichern.
    /// Rueckgabe: ob maprot/mapflip im Patch waren (dann gehoert die Ausrichtung zur Strecke).
    bool apply(const QJsonObject& patch);
    /// Ein einzelnes Feld intern setzen (Streckenausrichtung), speichert wenn geaendert.
    void set(const QString& key, const QJsonValue& value);
    void reset();

    QJsonObject presets() const { return m_presets; }
    /// Schnappschuss ALLER Settings (ausser preset) unter dem Namen. "" = Fehlertext.
    QString presetSichern(const QString& name);
    void presetLoeschen(const QString& name);

    QJsonObject layouts() const { return m_layouts; }
    QString layoutSichern(const QString& name);
    void layoutLoeschen(const QString& name);

    /// Dateinamen in :/static/logos (fuer tower_logo).
    static QStringList listLogos();
    /// .json-Dateien im Datenordner, championship.json immer zuerst.
    QStringList listChampFiles() const;
    /// Datei, aus der der WM-Stand gelesen wird (gewaehlt -> championship.json -> eingebettet).
    QString champPath() const;
    QString dataDir() const { return m_dataDir; }
    bool defaultChampExists() const;

private:
    void load();
    void save();
    void savePresets();
    void saveLayouts();

    QString m_dataDir;
    QJsonObject m_settings;
    QJsonObject m_presets;   // {name: {settings}}
    QJsonObject m_layouts;   // {name: {baustein: {...}}}
};

}  // namespace kers::server
