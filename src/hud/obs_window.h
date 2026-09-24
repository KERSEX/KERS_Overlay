// Das OBS-Fenster: dieselbe Overlay-Szene noch einmal, aber aufnahmetauglich.
// Portierung von hud/obs_window.py.
//
// Die Fensteraufnahme von OBS zeigt vom HUD-Fenster nur EIN Bild und friert
// dann ein: Qt setzt fuer ein Fenster mit Alphakanal WS_EX_LAYERED und zeichnet
// ueber DirectComposition direkt in den Desktop; die Windows Graphics Capture
// liest dagegen die DWM-Umleitungsflaeche, und die bekommt keine neuen Bilder.
//
// Also gibt es beides: das HUD-Fenster (durchsichtig, fuers Auge) und dieses
// hier - ein normales Fenster in Canvas-Groesse, undurchsichtig, Hintergrund in
// der Schluesselfarbe. Beide haengen an DERSELBEN Bruecke.
//
// ⚠ Eigenes Theme: im OBS-Fenster muessen alle Panels deckend sein (sonst frisst
// der Farbschluessel sie anteilig mit weg), auf dem Desktop bleiben sie durchsichtig.
#pragma once

#include "hud_state.h"
#include "theme.h"

#include <QColor>
#include <QObject>
#include <QQuickView>

namespace kers::hud {

class OverlayBridge;

/// Was die Szene als `Hud` sieht, wenn sie im OBS-Fenster laeuft: kein
/// Bearbeiten-Modus, Hintergrund immer die Schluesselfarbe, `locked` fest wahr.
class ObsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(bool layoutEdit READ layoutEdit NOTIFY layoutEditChanged)
    Q_PROPERTY(bool chroma READ chroma NOTIFY bgChanged)
    Q_PROPERTY(QColor chromaColor READ chromaColor NOTIFY bgChanged)
    Q_PROPERTY(bool findable READ findable NOTIFY bgChanged)
public:
    explicit ObsController(const QString& color, QObject* parent = nullptr) : QObject(parent), m_color(color) {}
    bool locked() const { return true; }
    bool layoutEdit() const { return false; }
    bool chroma() const { return true; }
    QColor chromaColor() const { return QColor(m_color); }
    bool findable() const { return true; }
    void setColor(const QString& color);
    // Die Szene ruft diese nur aus dem Bearbeiten-Rahmen, der hier nie sichtbar ist.
    Q_INVOKABLE void beginEdit(const QString&, int, int) {}
    Q_INVOKABLE void editTo(int, int) {}
    Q_INVOKABLE void endEdit() {}
signals:
    void lockedChanged(bool);
    void layoutEditChanged(bool);
    void bgChanged();
private:
    QString m_color;
};

/// Undurchsichtiges Fenster in Canvas-Groesse mit der Overlay-Szene darin.
class ObsWindow : public QQuickView {
    Q_OBJECT
public:
    static constexpr int DEFAULT_W = 1920;
    static constexpr int DEFAULT_H = 1080;

    ObsWindow(HudState& state, OverlayBridge* bridge, const QString& qmlDir = QString());

    void reloadScene();
    void setChromaColor(const QString& color);
    void setCanvas(int w, int h);
    /// Diagnose: hat das Fenster doch WS_EX_LAYERED? Dann stimmt am Format etwas nicht.
    bool isLayered() const;

private:
    void syncTheme();
    QUrl sceneUrl() const;

    HudState& m_state;
    QString m_qmlDir;
    Theme* m_theme;
    OverlayBridge* m_bridge;
    ObsController* m_hud;
};

}  // namespace kers::hud
