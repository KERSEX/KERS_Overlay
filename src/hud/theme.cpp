#include "theme.h"

#include "paths.h"

#include <QDir>
#include <QFontDatabase>
#include <QUrl>
#include <QtGlobal>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

const char* const kTextMain = "#ffffff";
const char* const kTextMuted = "#a6a6b4";
const char* const kAccentDefault = "#e10600";

QColor rgba(int r, int g, int b, qreal a)
{
    QColor c(r, g, b);
    c.setAlphaF(std::clamp(a, 0.0, 1.0));
    return c;
}

}  // namespace

QStringList loadFonts()
{
    QStringList loaded;
    QDir dir(paths::assetRootPath() + QStringLiteral("/fonts/ttf"));
    if (!dir.exists()) {
        qWarning() << "[HUD] Schriften fehlen:" << dir.path();
        return loaded;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.ttf")}, QDir::Files, QDir::Name);
    for (const QString& name : files) {
        if (QFontDatabase::addApplicationFont(dir.filePath(name)) == -1)
            qWarning() << "[HUD] Schrift nicht ladbar:" << name;
        else
            loaded << name;
    }
    if (loaded.isEmpty())
        qWarning() << "[HUD] Keine Schrift geladen - Overlay nutzt die Systemschrift.";
    return loaded;
}

const QHash<QString, QString>& Theme::teamColors()
{
    // Identisch zu :root in static/css/core.css und TEAM_COLORS in models.py.
    static const QHash<QString, QString> colors = {
        {"Red Bull", "#3671C6"}, {"McLaren", "#FF8000"}, {"Ferrari", "#E80020"},
        {"Mercedes", "#27F4D2"}, {"Aston Martin", "#229971"}, {"Haas", "#B6BABD"},
        {"RB", "#6692FF"}, {"Alpine", "#0093CC"}, {"Williams", "#64C4FF"},
        {"Sauber", "#52E252"}, {"Audi", "#C8102E"}, {"Cadillac", "#C9A227"},
    };
    return colors;
}

Theme::Theme(QObject* parent)
    : QObject(parent)
    , m_accent(QLatin1String(kAccentDefault))
    , m_assetRoot(paths::assetRootUrl())
{
}

void Theme::setOpaque(bool opaque)
{
    if (m_opaque == opaque)
        return;
    m_opaque = opaque;
    emit changed();
}

void Theme::applySettings(qreal uiAlpha, qreal textOutline, const QString& accentIn,
                          const QString& headerColor, const QString& rowColor)
{
    const QString accent = accentIn.isEmpty() ? QLatin1String(kAccentDefault) : accentIn;
    if (uiAlpha == m_uiAlpha && textOutline == m_textOutline && accent == m_accent
        && headerColor == m_headerAccent && rowColor == m_rowOverride)
        return;
    m_uiAlpha = uiAlpha;
    m_textOutline = textOutline;
    m_accent = QColor::isValidColorName(accent) ? accent : QLatin1String(kAccentDefault);
    m_headerAccent = QColor::isValidColorName(headerColor) ? headerColor : QString();
    m_rowOverride = QColor::isValidColorName(rowColor) ? rowColor : QString();
    emit changed();
}

// ── Grundfarben ──────────────────────────────────────────────────────────────
QColor Theme::accent() const
{
    return QColor(m_accent);
}

QColor Theme::accentGlow() const
{
    QColor c(m_accent);
    c.setAlphaF(0.5);
    return c;
}

QColor Theme::headerAccent() const
{
    return QColor(m_headerAccent.isEmpty() ? m_accent : m_headerAccent);
}

// ── Schrift ──────────────────────────────────────────────────────────────────
// ⚠ Die Deckkraft (uiAlpha) faerbt NUR die Flaechen ein. Die Textfarben sind voll
// deckend und bleiben es. Unlesbar wird die Schrift bei niedriger Deckkraft nicht,
// weil sie blasser wuerde, sondern weil der dunkle Grund verschwindet - dagegen
// hilft nur ein Rand um die Zeichen (textOutline).
QColor Theme::textStyleColor() const
{
    // Hoechstens 0,95 und nicht 1,0: reines Schwarz wirkt hart.
    return rgba(0, 0, 0, 0.7 + 0.25 * m_textOutline);
}

QColor Theme::textMain() const
{
    return QColor(QLatin1String(kTextMain));
}

QColor Theme::textMuted() const
{
    // Mit steigender Kontur Richtung Weiss aufgehellt: der Teamname unter dem
    // Fahrernamen war sonst auch mit Rand noch schwer zu lesen.
    if (m_textOutline <= 0)
        return QColor(QLatin1String(kTextMuted));
    const QColor ziel{QLatin1String(kTextMain)};
    const QColor jetzt{QLatin1String(kTextMuted)};
    const qreal t = m_textOutline;
    return QColor(
        static_cast<int>(std::lround(jetzt.red() + (ziel.red() - jetzt.red()) * t)),
        static_cast<int>(std::lround(jetzt.green() + (ziel.green() - jetzt.green()) * t)),
        static_cast<int>(std::lround(jetzt.blue() + (ziel.blue() - jetzt.blue()) * t)));
}

// ── Flaechen ─────────────────────────────────────────────────────────────────
QColor Theme::panelBg() const
{
    return rgba(14, 14, 19, 0.94 * alpha());
}

QColor Theme::rowBg() const
{
    if (!m_rowOverride.isEmpty())
        return QColor(m_rowOverride);
    return rgba(24, 24, 33, 0.988 * alpha());
}

QColor Theme::rowBgAlt() const
{
    if (!m_rowOverride.isEmpty())
        return QColor(m_rowOverride);
    return rgba(16, 16, 23, 0.988 * alpha());
}

QColor Theme::headerRowBg() const
{
    // .header-row - enthaelt den weggefallenen Container-Hintergrund mit.
    return rgba(10, 10, 15, 0.994 * alpha());
}

QColor Theme::towerHead1() const
{
    return rgba(22, 22, 30, alpha());
}

QColor Theme::towerHead2() const
{
    return rgba(40, 40, 50, alpha());
}

QColor Theme::headBg1() const
{
    return rgba(27, 27, 34, alpha());
}

QColor Theme::headBg2() const
{
    return rgba(17, 17, 22, alpha());
}

QColor Theme::panelBorder() const
{
    return rgba(255, 255, 255, 0.09);
}

// ── Asset-Pfade ──────────────────────────────────────────────────────────────
// Absolute URLs, von C++ gesetzt. Relative Pfade aus den QML-Dateien heraus
// waeren an die Ordnertiefe der jeweiligen Datei gebunden.
QString Theme::teamLogo(const QString& filename) const
{
    return filename.isEmpty() ? QString() : m_assetRoot + QStringLiteral("/teams/") + filename + QStringLiteral(".png");
}

QString Theme::tyre(const QString& compound) const
{
    return compound.isEmpty() ? QString() : m_assetRoot + QStringLiteral("/tyres/") + compound + QStringLiteral(".png");
}

QString Theme::damage(const QString& part) const
{
    return part.isEmpty() ? QString() : m_assetRoot + QStringLiteral("/damage/") + part + QStringLiteral(".png");
}

QString Theme::brandLogo(const QString& filename) const
{
    // Eigenes Logo im Tower-Kopf (static/logos/, Auswahl in den Settings).
    // Dateinamen enthalten Leerzeichen ("GFL Cut.png") - deshalb percent-encoded.
    if (filename.isEmpty())
        return QString();
    return m_assetRoot + QStringLiteral("/logos/") + QString::fromUtf8(QUrl::toPercentEncoding(filename));
}

QColor Theme::teamColor(const QString& team) const
{
    return QColor(teamColors().value(team, QStringLiteral("#ffffff")));
}

}  // namespace kers::hud
