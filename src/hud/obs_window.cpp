#include "obs_window.h"

#include "bridge.h"
#include "models.h"

#include <QDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSurfaceFormat>
#include <QDebug>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kers::hud {

void ObsController::setColor(const QString& color)
{
    if (color != m_color) {
        m_color = color;
        emit bgChanged();
    }
}

ObsWindow::ObsWindow(HudState& state, OverlayBridge* bridge, const QString& qmlDir)
    : QQuickView()
    , m_state(state)
    , m_qmlDir(qmlDir)
    , m_bridge(bridge)
{
    // ⚠ Eigenes Surface-Format OHNE Alphakanal, gesetzt VOR dem Erzeugen des
    // Fensters. Genau das ist der Unterschied zum HUD-Fenster: ohne Alpha macht
    // Windows daraus ein gewoehnliches Fenster mit Umleitungsflaeche, und die
    // Fensteraufnahme von OBS bekommt laufend neue Bilder.
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(0);
    setFormat(fmt);

    setTitle(QStringLiteral("KERS OBS"));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::Window);

    const QString color = state.obsChromaColor.isEmpty() ? QStringLiteral("#FF00FF") : state.obsChromaColor;
    setColor(QColor(color));

    m_theme = new Theme(this);
    m_theme->setOpaque(true);            // Panels deckend, siehe Kopfkommentar
    m_hud = new ObsController(color, this);

    SettingsState* s = m_bridge->settingsState();
    connect(s, &SettingsState::brandAccentChanged, this, &ObsWindow::syncTheme);
    connect(s, &SettingsState::headerColorChanged, this, &ObsWindow::syncTheme);
    connect(s, &SettingsState::rowColorChanged, this, &ObsWindow::syncTheme);
    connect(s, &SettingsState::uiAlphaChanged, this, &ObsWindow::syncTheme);
    connect(s, &SettingsState::textOutlineChanged, this, &ObsWindow::syncTheme);
    syncTheme();

    QQmlContext* ctx = engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("Theme"), m_theme);
    ctx->setContextProperty(QStringLiteral("Kers"), m_bridge);
    ctx->setContextProperty(QStringLiteral("Hud"), m_hud);

    connect(this, &QQuickView::statusChanged, this, [this](QQuickView::Status st) {
        if (st == QQuickView::Error)
            for (const QQmlError& e : errors())
                qCritical() << "[OBS] QML-Fehler:" << e.toString();
    });

    resize(state.obsW > 0 ? state.obsW : DEFAULT_W, state.obsH > 0 ? state.obsH : DEFAULT_H);
    setSource(sceneUrl());
}

QUrl ObsWindow::sceneUrl() const
{
    if (!m_qmlDir.isEmpty())
        return QUrl::fromLocalFile(QDir(m_qmlDir).filePath(QStringLiteral("Overlay.qml")));
    return QUrl(QStringLiteral("qrc:/KersHud/Overlay.qml"));
}

void ObsWindow::syncTheme()
{
    SettingsState* s = m_bridge->settingsState();
    m_theme->applySettings(s->uiAlpha(), s->textOutline(), s->brandAccent(), s->headerColor(), s->rowColor());
    m_theme->setOpaque(true);            // applySettings darf das nicht kippen
}

void ObsWindow::reloadScene()
{
    engine()->clearComponentCache();
    setSource(sceneUrl());
}

void ObsWindow::setChromaColor(const QString& color)
{
    m_state.obsChromaColor = color;
    setColor(QColor(color));
    m_hud->setColor(color);
}

void ObsWindow::setCanvas(int w, int h)
{
    m_state.obsW = w;
    m_state.obsH = h;
    resize(w, h);
}

bool ObsWindow::isLayered() const
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(const_cast<ObsWindow*>(this)->winId());
    return hwnd && (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
#else
    return false;
#endif
}

}  // namespace kers::hud
