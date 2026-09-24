// Das HUD-Fenster und sein Zustandsobjekt `Hud` - Portierung von hud/qml_overlay.py.
//
// Die Fenster-Technik:
//     Qt::FramelessWindowHint        kein Rahmen, keine Titelleiste
//     Qt::WindowStaysOnTopHint       bleibt ueber allen anderen Fenstern
//     Qt::Tool                       kein Taskbar- und kein Alt-Tab-Eintrag
//     Qt::WindowTransparentForInput  Klicks gehen durch (nur gesperrt)
//
// Transparenz kommt aus dem Alphakanal des Fensters: setColor(transparent) plus
// ein Alphapuffer im Surface-Format, der VOR der QApplication angemeldet wird
// (main.cpp).
//
// Dazu drei Win32-Handgriffe, die Qt nicht selbst macht (siehe .cpp):
//   * WS_EX_TOOLWINDOW nachtraeglich setzen/wegnehmen (OBS-Auffindbarkeit)
//   * alle 2 s SetWindowPos(HWND_TOPMOST) - "always on top" heisst nur "obere
//     Schicht", nicht "oberstes Fenster"
//   * Diagnose, WER sich davorlegt (DWM-Cloaking beachten)
#pragma once

#include "hud_state.h"
#include "theme.h"

#include <QColor>
#include <QObject>
#include <QQuickView>
#include <QRect>
#include <QString>
#include <QTimer>

#include <optional>

namespace kers::hud {

class OverlayBridge;
class HudWindow;

/// Der Fensterzustand, wie QML ihn sieht: gesperrt, Bearbeiten, OBS-Modus.
class HudController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    Q_PROPERTY(bool layoutEdit READ layoutEdit WRITE setLayoutEdit NOTIFY layoutEditChanged)
    Q_PROPERTY(bool chroma READ chroma WRITE setChroma NOTIFY bgChanged)
    Q_PROPERTY(QColor chromaColor READ chromaColor NOTIFY bgChanged)
    Q_PROPERTY(bool findable READ findable WRITE setFindable NOTIFY bgChanged)
public:
    // Magenta: kommt in einem Formel-1-Overlay garantiert nicht vor.
    static constexpr const char* DEFAULT_CHROMA = "#FF00FF";
    static constexpr int MIN_W = 240;
    static constexpr int MIN_H = 160;

    HudController(HudWindow* window, HudState& state, QObject* parent = nullptr);

    bool locked() const { return m_locked; }
    void setLocked(bool v);
    bool layoutEdit() const { return m_layoutEdit; }
    void setLayoutEdit(bool on);
    bool chroma() const { return m_chroma; }
    void setChroma(bool v);
    QColor chromaColor() const { return QColor(m_chromaColor); }
    bool findable() const { return m_findable; }
    void setFindable(bool v);

    // Bearbeiten-Modus: Fenster ziehen und skalieren (EditFrame.qml)
    Q_INVOKABLE void beginEdit(const QString& corner, int globalX, int globalY);
    Q_INVOKABLE void editTo(int globalX, int globalY);
    Q_INVOKABLE void endEdit();

    // Vom Fenster gerufen, nachdem es die Flags gesetzt hat
    void setLockedSilent(bool v);
    void setBgSilent(bool chroma, const QString& color);
    void setFindableSilent(bool v);

signals:
    void lockedChanged(bool);
    void layoutEditChanged(bool);
    void bgChanged();

private:
    HudWindow* m_window;
    HudState& m_state;
    bool m_locked;
    bool m_chroma;
    QString m_chromaColor;
    bool m_findable;
    QString m_editCorner;
    std::optional<QRect> m_originGeo;
    QPoint m_originMouse;
    bool m_layoutEdit = false;
    bool m_lockedVorher = true;
};

/// Rahmenloses Always-on-Top-Fenster mit der QML-Overlay-Szene darin.
class HudWindow : public QQuickView {
    Q_OBJECT
public:
    static constexpr int TOPMOST_INTERVAL_MS = 2000;

    HudWindow(HudState& state, const QString& demo = QString(), const QString& qmlDir = QString());
    ~HudWindow() override;

    HudState& state() { return m_state; }
    OverlayBridge* bridge() const { return m_bridge; }
    Theme* theme() const { return m_theme; }
    HudController* hud() const { return m_hud; }
    QUrl sceneUrl() const;

    void loadScene();
    void reloadPage();
    void start();
    void stop();
    void setBaseUrl(const QString& base);
    void setHz(int hz);

    void updateWindowFlags();
    void setFindable(bool on);
    void setLocked(bool locked);
    void toggleLocked();
    void setHudVisible(bool visible);
    void setChroma(bool on, const QString& color = QString());
    void setLayoutEdit(bool on);
    void layoutZuruecksetzen();
    void applyGeometry(int x, int y, int w, int h);
    void rememberGeometry();

signals:
    void hudGeometryChanged(int x, int y, int w, int h);
    void hudLockedChanged(bool locked);
    void hudVisibilityChanged(bool visible);
    void sceneLoaded(bool ok);
    void hudLayoutEditChanged(bool on);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void moveEvent(QMoveEvent* e) override;

private:
    friend class HudController;
    void syncTheme();
    void applyNativeToolwindow();
    void reassertTopmost();
    void diagnoseLage();
    QString fensterUeberUns() const;
    void syncTopmostTimer();

    HudState& m_state;
    QString m_qmlDir;
    bool m_wantVisible;
    Theme* m_theme;
    OverlayBridge* m_bridge;
    HudController* m_hud;
    QTimer m_topmost;
    QString m_letzterBefund;
};

}  // namespace kers::hud
