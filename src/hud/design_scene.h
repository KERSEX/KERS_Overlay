// Das aktive Design fuer EIN Fenster (HUD bzw. OBS-Fenster), ab 0.3.1.
//
// Setzt die Werte aus design.json ins Theme und stellt die Szene bereit (siehe
// src/common/designs.h). Jedes Fenster hat seinen eigenen Laufzeit-Ordner, damit
// das Neuladen des einen dem anderen nicht die Dateien unter den Fuessen wegzieht.
#pragma once

#include <QString>
#include <QUrl>

namespace kers::hud {

class Theme;

class DesignScene {
public:
    explicit DesignScene(const QString& slot)
        : m_slot(slot)
    {
    }

    /// Design uebernehmen und Szene zusammensetzen ("" = Standard).
    void apply(Theme* theme, const QString& id);
    /// Gleiches Design noch einmal zusammensetzen (Dateien geaendert, "Neu laden").
    void refresh(Theme* theme) { apply(theme, m_wanted); }
    QString id() const { return m_id; }
    QString wanted() const { return m_wanted; }
    /// Szene laden von: --qml-dir (Entwickeln), Laufzeit-Ordner oder eingebaut.
    QUrl url(const QString& qmlDir) const;

    /// Design aus data\overlay_settings.json - fuer den ersten Start, bevor die
    /// Settings ueber den Datenstrom kommen (sonst lueden beide Fenster zweimal).
    static QString initialId();

private:
    QString m_slot;
    QString m_wanted;
    QString m_id;
    QString m_dir;
};

}  // namespace kers::hud
