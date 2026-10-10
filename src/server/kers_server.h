// Der Server als Ganzes - Ersatz fuer main.py.
//
//   UDP (QUdpSocket) -> Recorder -> RaceState          (Pakete, Zustand)
//   HttpServer -> Routen (routes.cpp)                    (Seiten, API)
//   SseHub                                               (/api/stream)
//
// Laeuft im Prozess des HUD (ab M3) oder allein mit --server. Alles im Haupt-
// Thread; nach aussen nur Signale und Direktzugriffe, damit das HUD den Zustand
// ohne Umweg ueber HTTP bekommt.
#pragma once

#include "access_guard.h"
#include "cam_keys.h"
#include "http_server.h"
#include "obs_link.h"
#include "race_state.h"
#include "recorder.h"
#include "settings_store.h"
#include "sse_hub.h"
#include "tracks.h"

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUdpSocket>

#include <functional>
#include <memory>
#include <optional>

namespace kers::server {

class KersServer : public QObject {
    Q_OBJECT
public:
    /// dataDir: data\ neben der EXE (Settings, Presets, Layouts, tracks.json, WM, recordings\)
    /// builtinTracks: eingebaute Konturen (":/static/tracks.json")
    explicit KersServer(const QString& dataDir, const QString& builtinTracks = QStringLiteral(":/static/tracks.json"),
                        QObject* parent = nullptr);
    ~KersServer() override;

    /// HTTP und UDP oeffnen. false = ein Port ist belegt (errorString sagt welcher).
    bool start(quint16 httpPort, quint16 udpPort, const QHostAddress& bind = QHostAddress::AnyIPv4);
    void stop();
    bool isRunning() const { return m_running; }
    QString errorString() const { return m_error; }
    quint16 httpPort() const { return m_httpPort; }
    quint16 udpPort() const { return m_udpPort; }

    /// Wanduhr (Sekunden) - fuer Tests austauschbar.
    void setClock(std::function<double()> clock);
    double now() const { return m_clock(); }

    // ── Direktzugriffe (In-Process-HUD, Tests) ───────────────────────────────
    QJsonObject livePayload();
    QJsonObject trackJson();
    QJsonObject lapPositions() const;
    QJsonObject championship() const;
    QJsonObject status() const;
    QJsonObject settings() const { return m_settings.settings(); }
    /// Wie POST /api/settings: pruefen, uebernehmen, Streckenausrichtung merken.
    QJsonObject applySettings(const QJsonObject& patch);
    QJsonObject regie() const { return m_regie; }

    /// Ein Datagramm einarbeiten - genau der Weg der UDP-Pakete (inkl. Recorder).
    void feedPacket(const QByteArray& data);
    /// Eine HTTP-Anfrage beantworten (routes.cpp).
    HttpResponse handle(const HttpRequest& req);

    RaceState& state() { return *m_state; }
    SettingsStore& settingsStore() { return m_settings; }
    TrackStore& trackStore() { return m_tracks; }
    CamKeys& camKeys() { return m_cam; }
    /// Zugriffsschutz fuer fremde Geraete (Code, Geraeteliste, Sperren).
    AccessGuard& access() { return m_access; }
    /// OBS-Steuerung (obs-websocket), ab 0.3.1.
    ObsLink& obs() { return m_obs; }
    int streamClients() const { return m_sse.clientCount(); }

signals:
    void runningChanged(bool running);
    /// Nach jedem eingearbeiteten UDP-Paket (fuer den Takt des HUD).
    void packetReceived();
    /// Settings oder Regie wurden geaendert (HTTP oder direkt).
    void settingsChanged();

private:
    void onUdp();
    void neuerZustand();

    // Routen-Helfer (routes.cpp)
    /// Vor allen Routen: gesperrt? fremd ohne Code? fremde Webseite (CSRF)?
    std::optional<HttpResponse> wache(const HttpRequest& req, const AccessGuard::Result& zugang) const;
    HttpResponse route(const HttpRequest& req, const AccessGuard::Result& zugang);
    HttpResponse apiLogin(const HttpRequest& req);
    HttpResponse loginPage() const;
    HttpResponse page(const QString& templateName) const;
    HttpResponse partsIndex() const;
    HttpResponse staticFile(const HttpRequest& req, const QString& rel) const;
    HttpResponse apiCam(const HttpRequest& req);
    HttpResponse apiTracks(const HttpRequest& req);
    HttpResponse apiPresets(const HttpRequest& req);
    HttpResponse apiLayouts(const HttpRequest& req);
    HttpResponse apiChampPoints(const HttpRequest& req);
    HttpResponse apiObs(const HttpRequest& req, const AccessGuard::Result& zugang);
    HttpResponse apiDesigns(const HttpRequest& req, const AccessGuard::Result& zugang);
    /// Farben des aktiven Designs als CSS-Variablen (haengt an /static/css/core.css).
    QByteArray designCss() const;
    QJsonObject apiVersion() const;
    void relearn();

    QString m_dataDir;
    std::function<double()> m_clock;
    SettingsStore m_settings;
    AccessGuard m_access;
    TrackStore m_tracks;
    std::unique_ptr<RaceState> m_state;
    QJsonObject m_regie;
    Recorder m_recorder;
    CamKeys m_cam;
    HttpServer m_http;
    SseHub m_sse;
    ObsLink m_obs;
    EventChannel m_obsChannel;
    QUdpSocket m_udp;
    bool m_running = false;
    QString m_error;
    quint16 m_httpPort = 0;
    quint16 m_udpPort = 0;
};

}  // namespace kers::server
