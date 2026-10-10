// Das Gegenstueck zu :root in static/css/core.css - Portierung von hud/theme.py.
//
// Im Web-Overlay stehen alle Farben, Radien und Schattenwerte als CSS-Custom-
// Properties im :root, und applyBrand() in core.js schreibt zur Laufzeit hinein:
// Akzentfarbe aus den Settings, Deckkraft (--ui-alpha), Kopf- und Zeilenfarbe.
//
// Genau das ist diese Klasse: ein Objekt mit allen Tokens, das QML als
// Context-Property `Theme` sieht. Setzt die Bruecke uiAlpha oder accent, faerbt
// sich die ganze Szene mit um - dieselbe Mechanik wie im Browser, nur mit
// Qt-Signalen statt CSS-Kaskade.
//
// ⚠ Property-Namen sind die Schnittstelle zur unveraenderten QML-Szene. Nichts
// umbenennen; eine unbekannte Property ist in QML still `undefined`.
//
// Ab 0.3.1 kommen Farben, Deckkraft, Formen und Schriften aus dem aktiven Design
// (design.json, siehe src/common/designs.h). Ohne Design gelten genau die Werte
// von bis 0.3.0 - das eingebaute Design "KERS" schreibt sie nur noch einmal hin.
#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace kers::hud {

/// Inter und Teko aus :/static/fonts/ttf in die Fontdatenbank laden.
/// Rueckgabe: die geladenen Dateinamen. Leer = Systemschrift (Overlay sieht anders aus).
QStringList loadFonts();

class Theme : public QObject {
    Q_OBJECT
    // Deckkraft
    Q_PROPERTY(qreal uiAlpha READ uiAlpha NOTIFY changed)
    // Grundfarben
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentGlow READ accentGlow NOTIFY changed)
    Q_PROPERTY(QColor headerAccent READ headerAccent NOTIFY changed)
    // Schrift-Kontur
    Q_PROPERTY(qreal textOutline READ textOutline NOTIFY changed)
    Q_PROPERTY(QColor textStyleColor READ textStyleColor NOTIFY changed)
    Q_PROPERTY(QColor textMain READ textMain NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    // Flaechen (haengen alle an uiAlpha)
    Q_PROPERTY(QColor panelBg READ panelBg NOTIFY changed)
    Q_PROPERTY(QColor rowBg READ rowBg NOTIFY changed)
    Q_PROPERTY(QColor rowBgAlt READ rowBgAlt NOTIFY changed)
    Q_PROPERTY(QColor headerRowBg READ headerRowBg NOTIFY changed)
    Q_PROPERTY(QColor towerHead1 READ towerHead1 NOTIFY changed)
    Q_PROPERTY(QColor towerHead2 READ towerHead2 NOTIFY changed)
    Q_PROPERTY(QColor headBg1 READ headBg1 NOTIFY changed)
    Q_PROPERTY(QColor headBg2 READ headBg2 NOTIFY changed)
    Q_PROPERTY(QColor panelBorder READ panelBorder NOTIFY changed)
    // Masse
    Q_PROPERTY(int panelRadius READ panelRadius NOTIFY changed)
    Q_PROPERTY(int accentWidth READ accentWidth NOTIFY changed)
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY changed)
    // Schriften
    Q_PROPERTY(QString sans READ sans NOTIFY changed)
    Q_PROPERTY(QString display READ display NOTIFY changed)
    // Design (ab 0.3.1): Ordnername und freie Zusatzwerte aus design.json ("extra")
    // fuer eigene QML-Dateien eines Designs.
    Q_PROPERTY(QString design READ design NOTIFY changed)
    Q_PROPERTY(QVariantMap extra READ extra NOTIFY changed)
    // Asset-Pfade
    Q_PROPERTY(QString assetRoot READ assetRoot CONSTANT)

public:
    /// Zeilenhoehe des Towers - identisch zu ROW_HEIGHT in static/parts/tower.js.
    static constexpr int kRowHeight = 62;
    static const QHash<QString, QString>& teamColors();

    explicit Theme(QObject* parent = nullptr);

    /// Chroma-Key-Modus: ALLE Flaechen deckend (alpha 1.25 wie in core.css).
    void setOpaque(bool opaque);
    bool opaque() const { return m_opaque; }

    /// Gegenstueck zu applyBrand() in core.js - von der Bruecke gerufen.
    void applySettings(qreal uiAlpha, qreal textOutline, const QString& accent,
                       const QString& headerColor, const QString& rowColor);

    /// Design uebernehmen: Werte aus design.json, Schriften aus dir/fonts.
    /// Fehlende oder ungueltige Werte bleiben beim KERS-Standard.
    void applyDesign(const QString& id, const QJsonObject& tokens, const QString& dir);

    qreal uiAlpha() const { return m_uiAlpha; }
    QColor accent() const;
    QColor accentGlow() const;
    QColor headerAccent() const;
    qreal textOutline() const { return m_textOutline; }
    QColor textStyleColor() const;
    QColor textMain() const;
    QColor textMuted() const;
    QColor panelBg() const;
    QColor rowBg() const;
    QColor rowBgAlt() const;
    QColor headerRowBg() const;
    QColor towerHead1() const;
    QColor towerHead2() const;
    QColor headBg1() const;
    QColor headBg2() const;
    QColor panelBorder() const;
    int panelRadius() const { return m_t.panelRadius; }
    int accentWidth() const { return m_t.accentWidth; }
    int rowHeight() const { return m_t.rowHeight; }
    QString sans() const { return m_t.sans; }
    QString display() const { return m_t.display; }
    QString design() const { return m_design; }
    QVariantMap extra() const { return m_t.extra; }
    QString assetRoot() const { return m_assetRoot; }

    Q_INVOKABLE QString teamLogo(const QString& filename) const;
    Q_INVOKABLE QString tyre(const QString& compound) const;
    Q_INVOKABLE QString damage(const QString& part) const;
    Q_INVOKABLE QString brandLogo(const QString& filename) const;
    Q_INVOKABLE QColor teamColor(const QString& team) const;

signals:
    /// Alles, was von uiAlpha oder Branding abhaengt.
    void changed();

private:
    /// Die tatsaechlich verwendete Deckkraft (1.25 im Chroma-Modus).
    qreal alpha() const { return m_opaque ? 1.25 : m_uiAlpha; }

    /// Die Werte eines Designs. Vorgaben = der Look bis 0.3.0.
    struct Tokens {
        QColor accent{0xe1, 0x06, 0x00};
        QColor textMain{0xff, 0xff, 0xff};
        QColor textMuted{0xa6, 0xa6, 0xb4};
        // Flaechen: Farbe und Grund-Deckkraft (wird noch mit uiAlpha multipliziert)
        QColor panel{14, 14, 19};       qreal panelA = 0.94;
        QColor row{24, 24, 33};         qreal rowA = 0.988;
        QColor rowAlt{16, 16, 23};      qreal rowAltA = 0.988;
        QColor headerRow{10, 10, 15};   qreal headerRowA = 0.994;
        QColor towerHead1{22, 22, 30};  qreal towerHead1A = 1.0;
        QColor towerHead2{40, 40, 50};  qreal towerHead2A = 1.0;
        QColor headBg1{27, 27, 34};     qreal headBg1A = 1.0;
        QColor headBg2{17, 17, 22};     qreal headBg2A = 1.0;
        QColor panelBorder{255, 255, 255}; qreal panelBorderA = 0.09;   // NICHT an uiAlpha
        int panelRadius = 10;
        int accentWidth = 3;
        int rowHeight = kRowHeight;
        QString sans = QStringLiteral("Inter");
        QString display = QStringLiteral("Teko");
        QVariantMap extra;
    };
    QColor flaeche(const QColor& c, qreal a) const;

    Tokens m_t;
    QString m_design;
    QSet<QString> m_fontDirs;          // schon geladene Schrift-Ordner
    qreal m_uiAlpha = 1.0;
    qreal m_textOutline = 0.0;
    bool m_opaque = false;
    QString m_accent;          // aus den Settings ("#e10600" ab Werk)
    QString m_headerAccent;    // leer = Akzentfarbe verwenden
    QString m_rowOverride;     // leer = die beiden Standard-Zeilentoene
    QString m_assetRoot;
};

}  // namespace kers::hud
