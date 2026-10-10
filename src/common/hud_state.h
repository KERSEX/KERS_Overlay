// Der gespeicherte Zustand des HUD - Portierung von DEFAULT_STATE/load_state/
// save_state in hud/common.py. Datei: data\hud_state.json.
//
// Gelesen werden nur bekannte Schluessel; die alten Schluessel der Python-Fassung
// (page, renderer, zoom - alles Web-Renderer) werden beim Lesen ignoriert und
// nicht mehr geschrieben.
#pragma once

#include <QString>

namespace kers {

struct HudState {
    QString baseUrl = QStringLiteral("http://127.0.0.1:5100");
    int x = 60, y = 60, w = 780, h = 900;
    // Gesperrt = Klicks gehen durch zum Spiel. Normalfall im Betrieb, deshalb an.
    bool locked = true;
    bool visible = true;
    int opacity = 100;          // Fenster-Deckkraft in Prozent
    int hz = 30;                // Overlay-Renderrate
    // OBS - siehe hud/README.md "Overlay in OBS holen"
    bool obsWindow = false;
    int obsW = 1920, obsH = 1080;
    bool obsFindable = false;
    bool obsChroma = false;
    QString obsChromaColor = QStringLiteral("#FF00FF");
    int panelX = 900, panelY = 60;
    bool panelVisible = true;
    bool panelOnTop = false;
    bool updateStumm = false;
    // Neu in der C++-Fassung: der Server laeuft im selben Prozess und startet
    // beim Programmstart von selbst (Knopf im Schaltbrett bleibt).
    bool serverAutostart = true;
    // Ab 0.3.1: Schluessel fuer einen fremden Server (--url), siehe AccessGuard.
    QString remoteKey;

    static HudState load(const QString& path);
    bool save(const QString& path) const;
};

}  // namespace kers
