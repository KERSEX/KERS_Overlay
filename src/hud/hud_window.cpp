#include "hud_window.h"

#include "bridge.h"
#include "models.h"
#include "paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTextStream>
#include <QDebug>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace kers::hud {

namespace {

#ifdef Q_OS_WIN
// Die Taskleiste ist selbst ein Vordergrundfenster und liegt beim bildschirm-
// fuellenden HUD staendig darueber - sie waere die haeufigste Meldung im Log.
bool klasseEgal(const QString& klasse)
{
    return klasse == QLatin1String("Shell_TrayWnd") || klasse == QLatin1String("Shell_SecondaryTrayWnd");
}

// Wie weit die Diagnose die Fensterreihenfolge hochlaeuft, bevor sie aufgibt.
constexpr int MAX_ZORDER_WALK = 60;

QString fensterklasse(HWND h)
{
    wchar_t buf[128] = {};
    GetClassNameW(h, buf, 128);
    return QString::fromWCharArray(buf);
}

bool istWeggeblendet(HWND h)
{
    // DWM-Cloaking: Fenster gilt als sichtbar, ist aber weggeblendet (UWP).
    int wert = 0;
    const HRESULT hr = DwmGetWindowAttribute(h, DWMWA_CLOAKED, &wert, sizeof(wert));
    return SUCCEEDED(hr) && wert != 0;
}

QString fensterBeschreiben(HWND h)
{
    wchar_t titel[160] = {};
    GetWindowTextW(h, titel, 160);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    const QString eigen = pid == GetCurrentProcessId() ? QStringLiteral(" [wir selbst]") : QString();
    return QStringLiteral("'%1' \"%2\" (PID %3)%4").arg(fensterklasse(h), QString::fromWCharArray(titel)).arg(pid).arg(eigen);
}
#endif

}  // namespace

// ── HudController ────────────────────────────────────────────────────────────
HudController::HudController(HudWindow* window, HudState& state, QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_state(state)
    , m_locked(state.locked)
    , m_chroma(state.obsChroma)
    , m_chromaColor(state.obsChromaColor.isEmpty() ? QString::fromLatin1(DEFAULT_CHROMA) : state.obsChromaColor)
    , m_findable(state.obsFindable)
{
}

void HudController::setLocked(bool v)
{
    m_window->setLocked(v);
}

void HudController::setLockedSilent(bool v)
{
    if (m_locked != v) {
        m_locked = v;
        emit lockedChanged(v);
    }
}

void HudController::setLayoutEdit(bool on)
{
    // ⚠ Gesperrt gehen alle Klicks durch das Fenster hindurch - zum Bearbeiten
    // muss also entsperrt werden. Beim Ausschalten wird der vorherige Zustand
    // wiederhergestellt.
    if (on == m_layoutEdit)
        return;
    if (on) {
        m_lockedVorher = m_state.locked;
        if (m_lockedVorher)
            m_window->setLocked(false);
        // Fokus holen, damit Esc ankommt (das Fenster wird sonst bewusst ohne
        // Fokus gezeigt, SW_SHOWNOACTIVATE).
        m_window->requestActivate();
    }
    m_layoutEdit = on;
    // Erfundene Daten einspeisen: ohne laufendes Rennen waere die Szene leer.
    m_window->bridge()->setVorschau(on);
    emit layoutEditChanged(on);
    if (!on && m_lockedVorher)
        m_window->setLocked(true);
    // Nach draussen melden: der Fertig-Knopf sitzt auch IN der Szene.
    emit m_window->hudLayoutEditChanged(on);
}

void HudController::setChroma(bool v)
{
    m_window->setChroma(v);
}

void HudController::setBgSilent(bool chroma, const QString& color)
{
    if (m_chroma == chroma && m_chromaColor == color)
        return;
    m_chroma = chroma;
    m_chromaColor = color;
    emit bgChanged();
}

void HudController::setFindable(bool v)
{
    m_window->setFindable(v);
}

void HudController::setFindableSilent(bool v)
{
    if (m_findable != v) {
        m_findable = v;
        emit bgChanged();
    }
}

// Die Rechnerei ist die aus EditGlass._apply_resize: die gegenueberliegende Ecke
// bleibt stehen, und unter MIN_W/MIN_H geht es nicht.
void HudController::beginEdit(const QString& corner, int globalX, int globalY)
{
    m_editCorner = corner;
    m_originGeo = m_window->geometry();
    m_originMouse = QPoint(globalX, globalY);
}

void HudController::editTo(int globalX, int globalY)
{
    if (!m_originGeo)
        return;
    const int dx = globalX - m_originMouse.x();
    const int dy = globalY - m_originMouse.y();
    const QRect geo = *m_originGeo;

    if (m_editCorner.isEmpty()) {                 // ziehen = verschieben
        m_window->setPosition(geo.x() + dx, geo.y() + dy);
        return;
    }
    int x = geo.x(), y = geo.y(), w = geo.width(), h = geo.height();
    const bool left = m_editCorner.contains(QLatin1Char('l'));
    const bool top = m_editCorner.contains(QLatin1Char('t'));
    if (left) {
        x = geo.x() + dx;
        w = geo.width() - dx;
    } else {
        w = geo.width() + dx;
    }
    if (top) {
        y = geo.y() + dy;
        h = geo.height() - dy;
    } else {
        h = geo.height() + dy;
    }
    if (w < MIN_W) {
        w = MIN_W;
        if (left)
            x = geo.x() + geo.width() - MIN_W;
    }
    if (h < MIN_H) {
        h = MIN_H;
        if (top)
            y = geo.y() + geo.height() - MIN_H;
    }
    m_window->setGeometry(x, y, w, h);
}

void HudController::endEdit()
{
    m_editCorner.clear();
    m_originGeo.reset();
    m_window->rememberGeometry();
}

// ── HudWindow ────────────────────────────────────────────────────────────────
HudWindow::HudWindow(HudState& state, const QString& demo, const QString& qmlDir)
    : QQuickView()
    , m_state(state)
    , m_qmlDir(qmlDir)
    , m_wantVisible(state.visible)
{
    setTitle(QStringLiteral("KERS HUD"));
    setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setColor(QColor(0, 0, 0, 0));
    setResizeMode(QQuickView::SizeRootObjectToView);

    // Die drei Objekte, die die Szene sieht. Muessen vor setSource() stehen.
    m_theme = new Theme(this);
    m_bridge = new OverlayBridge(state.baseUrl, state.hz, demo, this);
    m_hud = new HudController(this, state, this);
    // Branding und Deckkraft wandern aus den Settings ins Theme - das
    // Gegenstueck zu applyBrand() in core.js.
    SettingsState* s = m_bridge->settingsState();
    connect(s, &SettingsState::brandAccentChanged, this, &HudWindow::syncTheme);
    connect(s, &SettingsState::headerColorChanged, this, &HudWindow::syncTheme);
    connect(s, &SettingsState::rowColorChanged, this, &HudWindow::syncTheme);
    connect(s, &SettingsState::uiAlphaChanged, this, &HudWindow::syncTheme);
    connect(s, &SettingsState::textOutlineChanged, this, &HudWindow::syncTheme);
    syncTheme();

    QQmlContext* ctx = engine()->rootContext();
    ctx->setContextProperty(QStringLiteral("Theme"), m_theme);
    ctx->setContextProperty(QStringLiteral("Kers"), m_bridge);
    ctx->setContextProperty(QStringLiteral("Hud"), m_hud);

    connect(this, &QQuickView::statusChanged, this, [this](QQuickView::Status st) {
        if (st == QQuickView::Error) {
            for (const QQmlError& e : errors())
                qCritical() << "[HUD] QML-Fehler:" << e.toString();
            emit sceneLoaded(false);
        } else if (st == QQuickView::Ready) {
            emit sceneLoaded(true);
        }
    });

    m_topmost.setInterval(TOPMOST_INTERVAL_MS);
    connect(&m_topmost, &QTimer::timeout, this, &HudWindow::reassertTopmost);

    setGeometry(state.x, state.y, state.w, state.h);
    setOpacity(state.opacity / 100.0);
    setChroma(state.obsChroma, state.obsChromaColor.isEmpty() ? QString::fromLatin1(HudController::DEFAULT_CHROMA) : state.obsChromaColor);
    loadScene();
    updateWindowFlags();
}

HudWindow::~HudWindow()
{
    m_topmost.stop();
    if (m_bridge)
        m_bridge->stop();
}

QUrl HudWindow::sceneUrl() const
{
    if (!m_qmlDir.isEmpty())
        return QUrl::fromLocalFile(QDir(m_qmlDir).filePath(QStringLiteral("Overlay.qml")));
    return QUrl(QStringLiteral("qrc:/KersHud/Overlay.qml"));
}

void HudWindow::loadScene()
{
    setSource(sceneUrl());
}

void HudWindow::reloadPage()
{
    // Ohne clearComponentCache laedt die Engine die alten, bereits uebersetzten
    // QML-Dateien wieder aus dem Cache.
    engine()->clearComponentCache();
    loadScene();
}

void HudWindow::syncTheme()
{
    SettingsState* s = m_bridge->settingsState();
    m_theme->applySettings(s->uiAlpha(), s->textOutline(), s->brandAccent(), s->headerColor(), s->rowColor());
}

void HudWindow::start()
{
    m_bridge->start();
}

void HudWindow::stop()
{
    m_bridge->stop();
}

void HudWindow::setBaseUrl(const QString& base)
{
    m_state.baseUrl = base;
    while (m_state.baseUrl.endsWith(QLatin1Char('/')))
        m_state.baseUrl.chop(1);
    m_bridge->setBaseUrl(m_state.baseUrl);
}

void HudWindow::setHz(int hz)
{
    m_state.hz = hz;
    m_bridge->setHz(hz);
}

// ── Fensterflags ─────────────────────────────────────────────────────────────
void HudWindow::updateWindowFlags()
{
    Qt::WindowFlags f = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
    // ⚠ Qt::Tool ist der Grund, warum OBS das HUD nicht findet: Qt setzt dafuer
    // WS_EX_TOOLWINDOW, und OBS wirft jedes Fenster mit diesem Stil aus seiner
    // Liste. Derselbe Stil unterdrueckt den Taskleisten-Eintrag - beides haengt
    // an derselben Eigenschaft.
    if (!m_state.obsFindable)
        f |= Qt::Tool;
    if (m_state.locked)
        f |= Qt::WindowTransparentForInput;

    const QRect geo = geometry();
    setFlags(f);              // macht das Fenster unter Windows unsichtbar ...
    setGeometry(geo);         // ... und vergisst dabei gerne die Geometrie
    if (m_wantVisible)
        show();
    applyNativeToolwindow();
    // setFlags() baut das native Fenster neu auf - danach steht es zwar wieder
    // in der Vordergrund-Schicht, aber nicht zwingend vorn.
    reassertTopmost();
    syncTopmostTimer();
}

void HudWindow::applyNativeToolwindow()
{
    // ⚠ Qt wertet Qt::Tool nur aus, wenn es das native Fenster ERZEUGT. Ein
    // spaeteres setFlags() laesst den erweiterten Fensterstil unveraendert
    // (nachgemessen: der Wert blieb bei 0x000800A8). Deshalb direkt ueber die
    // Win32-API; zum Umsetzen muss das Fenster kurz verschwinden.
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool wantTool = !m_state.obsFindable;
    const LONG_PTR target = wantTool ? (current | WS_EX_TOOLWINDOW) : (current & ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW));
    if (target == current)
        return;
    const bool visible = isVisible();
    if (visible)
        ShowWindow(hwnd, SW_HIDE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, target);
    if (visible)
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);    // dem Spiel nicht den Fokus klauen
#endif
}

// ── Vordergrund halten ───────────────────────────────────────────────────────
void HudWindow::reassertTopmost()
{
    // ⚠ WS_EX_TOPMOST sagt nur, dass das Fenster in die OBERE Schicht gehoert -
    // nicht, dass es dort das oberste ist. Jedes Fenster, das sich SPAETER als
    // Vordergrundfenster anmeldet (Spiel-Overlays, Discord, OBS-Projektor,
    // Benachrichtigungen), legt sich davor. SetWindowPos(HWND_TOPMOST) meldet
    // uns erneut oben an. NOACTIVATE ist Pflicht, NOOWNERZORDER laesst das
    // Besitzerfenster (Qt::Tool) in Ruhe.
#ifdef Q_OS_WIN
    if (!isVisible())
        return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    diagnoseLage();
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
#endif
}

void HudWindow::diagnoseLage()
{
    // Schreibt mit, sobald sich das HUD verdeckt vorfindet - und zwar NUR bei
    // einer Aenderung, sonst stuende alle 2 s dieselbe Zeile im Log.
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const bool hatFlag = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const QString darueber = fensterUeberUns();
    QString befund;
    if (hatFlag && darueber.isEmpty())
        befund.clear();
    else if (!hatFlag)
        befund = QStringLiteral("WS_EX_TOPMOST verloren; darueber: %1").arg(darueber.isEmpty() ? QStringLiteral("nichts") : darueber);
    else
        befund = QStringLiteral("verdeckt von %1").arg(darueber);
    if (befund == m_letzterBefund)
        return;
    m_letzterBefund = befund;
    if (befund.isEmpty())
        return;
    const QString zeile = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("  ") + befund;
    qInfo() << "[HUD]" << zeile;
    QFile f(paths::dataFile(QStringLiteral("hud_topmost.log")));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << zeile << '\n';
    }
#endif
}

QString HudWindow::fensterUeberUns() const
{
    // Das naechste SICHTBARE Fenster ueber uns - leer: keins. IsWindowVisible
    // allein reicht nicht: UWP-Fenster gelten als sichtbar, sind aber "cloaked".
#ifdef Q_OS_WIN
    HWND cur = GetWindow(reinterpret_cast<HWND>(winId()), GW_HWNDPREV);
    for (int i = 0; i < MAX_ZORDER_WALK; ++i) {
        if (!cur)
            return QString();
        if (IsWindowVisible(cur) && !istWeggeblendet(cur) && !klasseEgal(fensterklasse(cur)))
            return fensterBeschreiben(cur);
        cur = GetWindow(cur, GW_HWNDPREV);
    }
    return QStringLiteral("(Reihenfolge zu lang - abgebrochen)");
#else
    return QString();
#endif
}

void HudWindow::syncTopmostTimer()
{
    if (isVisible()) {
        if (!m_topmost.isActive())
            m_topmost.start();
    } else {
        m_topmost.stop();
    }
}

// ── Schalter ─────────────────────────────────────────────────────────────────
void HudWindow::setLayoutEdit(bool on)
{
    m_hud->setLayoutEdit(on);
}

void HudWindow::layoutZuruecksetzen()
{
    // Ein leeres Layout geht denselben Weg wie eine Verschiebung.
    m_bridge->layoutSpeichern(QVariantMap());
}

void HudWindow::setFindable(bool on)
{
    m_state.obsFindable = on;
    applyNativeToolwindow();
    reassertTopmost();
    m_hud->setFindableSilent(on);
}

void HudWindow::setLocked(bool locked)
{
    m_state.locked = locked;
    updateWindowFlags();
    m_hud->setLockedSilent(locked);
    emit hudLockedChanged(locked);
}

void HudWindow::toggleLocked()
{
    setLocked(!m_state.locked);
}

void HudWindow::setHudVisible(bool visible)
{
    m_wantVisible = visible;
    m_state.visible = visible;
    setVisible(visible);
    reassertTopmost();
    syncTopmostTimer();
    emit hudVisibilityChanged(visible);
}

void HudWindow::setChroma(bool on, const QString& colorIn)
{
    // Zwischen Alphakanal und einfarbigem Hintergrund umschalten (Rueckfallebene
    // fuer OBS-Aufnahmen ohne Alpha). Im Chroma-Modus muessen die Panels
    // deckend werden - sonst frisst der Farb-Key sie anteilig mit.
    QString color = colorIn.isEmpty() ? m_state.obsChromaColor : colorIn;
    if (color.isEmpty())
        color = QString::fromLatin1(HudController::DEFAULT_CHROMA);
    m_state.obsChroma = on;
    m_state.obsChromaColor = color;
    setColor(on ? QColor(color) : QColor(0, 0, 0, 0));
    m_theme->setOpaque(on);
    m_hud->setBgSilent(on, color);
}

// ── Geometrie ────────────────────────────────────────────────────────────────
void HudWindow::applyGeometry(int x, int y, int w, int h)
{
    setGeometry(x, y, std::max(HudController::MIN_W, w), std::max(HudController::MIN_H, h));
    rememberGeometry();
}

void HudWindow::rememberGeometry()
{
    const QRect geo = geometry();
    m_state.x = geo.x();
    m_state.y = geo.y();
    m_state.w = geo.width();
    m_state.h = geo.height();
    emit hudGeometryChanged(geo.x(), geo.y(), geo.width(), geo.height());
}

void HudWindow::resizeEvent(QResizeEvent* e)
{
    QQuickView::resizeEvent(e);
    rememberGeometry();
}

void HudWindow::moveEvent(QMoveEvent* e)
{
    QQuickView::moveEvent(e);
    rememberGeometry();
}

}  // namespace kers::hud
