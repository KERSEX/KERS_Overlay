#include "theme.h"

#include "paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFontDatabase>
#include <QJsonArray>
#include <QUrl>
#include <QtGlobal>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace kers::hud {

namespace {

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

// ── Design ───────────────────────────────────────────────────────────────────
void Theme::applyDesign(const QString& id, const QJsonObject& tokens, const QString& dir)
{
    Tokens t;                                   // erst alles auf Standard
    const QJsonObject farben = tokens.value("colors").toObject();
    const QJsonObject alpha = tokens.value("alpha").toObject();
    auto farbe = [&](const char* key, QColor& ziel) {
        const QString v = farben.value(QLatin1String(key)).toString().trimmed();
        if (QColor::isValidColorName(v))
            ziel = QColor(v);
    };
    auto deck = [&](const char* key, qreal& ziel) {
        const QJsonValue v = alpha.value(QLatin1String(key));
        if (v.isDouble())
            ziel = std::clamp(v.toDouble(), 0.0, 1.0);
    };
    farbe("accent", t.accent);
    farbe("textMain", t.textMain);
    farbe("textMuted", t.textMuted);
    farbe("panel", t.panel);           deck("panel", t.panelA);
    farbe("row", t.row);               deck("row", t.rowA);
    farbe("rowAlt", t.rowAlt);         deck("rowAlt", t.rowAltA);
    farbe("headerRow", t.headerRow);   deck("headerRow", t.headerRowA);
    farbe("towerHead1", t.towerHead1); deck("towerHead1", t.towerHead1A);
    farbe("towerHead2", t.towerHead2); deck("towerHead2", t.towerHead2A);
    farbe("headBg1", t.headBg1);       deck("headBg1", t.headBg1A);
    farbe("headBg2", t.headBg2);       deck("headBg2", t.headBg2A);
    farbe("panelBorder", t.panelBorder); deck("panelBorder", t.panelBorderA);
    const QJsonObject form = tokens.value("shape").toObject();
    auto zahl = [&](const char* key, int& ziel, int lo, int hi) {
        const QJsonValue v = form.value(QLatin1String(key));
        if (v.isDouble())
            ziel = std::clamp(static_cast<int>(std::lround(v.toDouble())), lo, hi);
    };
    zahl("panelRadius", t.panelRadius, 0, 40);
    zahl("accentWidth", t.accentWidth, 0, 12);
    zahl("rowHeight", t.rowHeight, 24, 120);

    // Schriften des Designs laden (einmal je Ordner), dann die Familien nehmen -
    // aber nur, wenn es sie wirklich gibt. Sonst bliebe Text in der Systemschrift.
    if (!dir.isEmpty() && !m_fontDirs.contains(dir)) {
        m_fontDirs.insert(dir);
        QDirIterator it(QDir(dir).filePath(QStringLiteral("fonts")), {QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (QFontDatabase::addApplicationFont(f) == -1)
                qWarning().noquote() << "[DESIGN] Schrift nicht ladbar:" << f;
        }
    }
    const QJsonObject schrift = tokens.value("fonts").toObject();
    const QStringList familien = QFontDatabase::families();
    auto familie = [&](const char* key, QString& ziel) {
        // Ein String oder eine Liste von Wunschfamilien - die erste vorhandene gewinnt
        // ("Formula1 Display" vor dem freien Ersatz, falls jemand die Originale ablegt).
        QStringList wunsch;
        const QJsonValue v = schrift.value(QLatin1String(key));
        if (v.isString())
            wunsch << v.toString();
        for (const QJsonValue& w : v.toArray())
            wunsch << w.toString();
        for (const QString& w : std::as_const(wunsch)) {
            if (familien.contains(w, Qt::CaseInsensitive)) {
                ziel = w;
                return;
            }
        }
        if (!wunsch.isEmpty())
            qWarning().noquote() << "[DESIGN] Schrift fehlt:" << wunsch.join(QStringLiteral(" / ")) << "- bleibt bei" << ziel;
    };
    familie("sans", t.sans);
    familie("display", t.display);
    t.extra = tokens.value("extra").toObject().toVariantMap();

    m_t = t;
    m_design = id;
    emit changed();
}

QColor Theme::flaeche(const QColor& c, qreal a) const
{
    return rgba(c.red(), c.green(), c.blue(), a * alpha());
}

// ── Grundfarben ──────────────────────────────────────────────────────────────
// Akzent: die Farbe aus den Settings gewinnt, sobald sie vom Werkszustand
// abweicht - sonst die des Designs.
QColor Theme::accent() const
{
    if (m_accent.compare(QLatin1String(kAccentDefault), Qt::CaseInsensitive) != 0)
        return QColor(m_accent);
    return m_t.accent;
}

QColor Theme::accentGlow() const
{
    QColor c = accent();
    c.setAlphaF(0.5);
    return c;
}

QColor Theme::headerAccent() const
{
    return m_headerAccent.isEmpty() ? accent() : QColor(m_headerAccent);
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
    return m_t.textMain;
}

QColor Theme::textMuted() const
{
    // Mit steigender Kontur Richtung Weiss aufgehellt: der Teamname unter dem
    // Fahrernamen war sonst auch mit Rand noch schwer zu lesen.
    if (m_textOutline <= 0)
        return m_t.textMuted;
    const QColor ziel = m_t.textMain;
    const QColor jetzt = m_t.textMuted;
    const qreal t = m_textOutline;
    return QColor(
        static_cast<int>(std::lround(jetzt.red() + (ziel.red() - jetzt.red()) * t)),
        static_cast<int>(std::lround(jetzt.green() + (ziel.green() - jetzt.green()) * t)),
        static_cast<int>(std::lround(jetzt.blue() + (ziel.blue() - jetzt.blue()) * t)));
}

// ── Flaechen ─────────────────────────────────────────────────────────────────
QColor Theme::panelBg() const
{
    return flaeche(m_t.panel, m_t.panelA);
}

QColor Theme::rowBg() const
{
    if (!m_rowOverride.isEmpty())
        return QColor(m_rowOverride);
    return flaeche(m_t.row, m_t.rowA);
}

QColor Theme::rowBgAlt() const
{
    if (!m_rowOverride.isEmpty())
        return QColor(m_rowOverride);
    return flaeche(m_t.rowAlt, m_t.rowAltA);
}

QColor Theme::headerRowBg() const
{
    // .header-row - enthaelt den weggefallenen Container-Hintergrund mit.
    return flaeche(m_t.headerRow, m_t.headerRowA);
}

QColor Theme::towerHead1() const
{
    return flaeche(m_t.towerHead1, m_t.towerHead1A);
}

QColor Theme::towerHead2() const
{
    return flaeche(m_t.towerHead2, m_t.towerHead2A);
}

QColor Theme::headBg1() const
{
    return flaeche(m_t.headBg1, m_t.headBg1A);
}

QColor Theme::headBg2() const
{
    return flaeche(m_t.headBg2, m_t.headBg2A);
}

QColor Theme::panelBorder() const
{
    return rgba(m_t.panelBorder.red(), m_t.panelBorder.green(), m_t.panelBorder.blue(), m_t.panelBorderA);
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
