// Das Bindeglied zwischen Datenquelle und QML-Szene - Portierung von hud/bridge.py.
//
// Der Weg eines Payloads:
//
//     Feed (SSE / lokal / Demo)  ->  OverlayBridge::onPayload
//                                    |- Config::apply          Settings uebernehmen
//                                    |- SharedState::maybeHoldQuali
//                                    |- SharedState::derive    gemeinsame Buchhaltung
//                                    |- SessionState           Kopfzeile
//                                    |- DriverModel::update    Fahrerzeilen
//                                    `- die Bausteine
//                                ->  QML liest die Properties und zeichnet
//
// In QML liegt das Objekt unter dem Namen `Kers`. Die Aufteilung in viele kleine
// Objekte ist Absicht: aendert sich der Session-Titel, sollen nicht die Bindings
// der Settings mit neu rechnen.
#pragma once

#include "data_source.h"
#include "derive.h"
#include "extras.h"
#include "feed.h"
#include "models.h"
#include "parts.h"

#include <QJsonObject>
#include <QObject>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <functional>
#include <optional>

namespace kers::hud {

class OverlayBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* drivers READ drivers CONSTANT)
    Q_PROPERTY(QObject* session READ session CONSTANT)
    Q_PROPERTY(QObject* settings READ settings CONSTANT)
    Q_PROPERTY(QObject* regie READ regie CONSTANT)
    Q_PROPERTY(int tick READ tick NOTIFY tickChanged)
    Q_PROPERTY(QObject* banner READ banner CONSTANT)
    Q_PROPERTY(QObject* battles READ battles CONSTANT)
    Q_PROPERTY(QObject* hotlaps READ hotlaps CONSTANT)
    Q_PROPERTY(QObject* onboard READ onboard CONSTANT)
    Q_PROPERTY(QObject* lowerThird READ lowerThird CONSTANT)
    Q_PROPERTY(QObject* pit READ pit CONSTANT)
    Q_PROPERTY(QObject* lights READ lights CONSTANT)
    Q_PROPERTY(QObject* fastestLap READ fastestLap CONSTANT)
    Q_PROPERTY(QObject* danger READ danger CONSTANT)
    Q_PROPERTY(QObject* pitProjection READ pitProjection CONSTANT)
    Q_PROPERTY(QObject* trackmap READ trackmap CONSTANT)
    Q_PROPERTY(QObject* charts READ charts CONSTANT)
    Q_PROPERTY(QObject* championship READ championship CONSTANT)
    Q_PROPERTY(bool layoutOffen READ layoutOffen NOTIFY layoutOffenChanged)
public:
    // Wie lange der rote Rahmen nach einer roten Flagge stehen bleibt (tower.js: 15 s).
    static constexpr double RED_FLAG_MS = 15000;

    /// demo: "" (Server), "race" oder "quali" (erfundenes Rennen statt Server).
    OverlayBridge(const QString& baseUrl, int hz = 30, const QString& demo = QString(),
                  QObject* parent = nullptr);
    ~OverlayBridge() override;

    QObject* drivers() const { return m_drivers; }
    SettingsState* settingsState() const { return m_settings; }
    QObject* session() const { return m_session; }
    QObject* settings() const { return m_settings; }
    QObject* regie() const { return m_regie; }
    int tick() const { return m_tick; }
    QObject* banner() const { return m_banner; }
    QObject* battles() const { return m_battles; }
    QObject* hotlaps() const { return m_hotlaps; }
    QObject* onboard() const { return m_onboard; }
    QObject* lowerThird() const { return m_lowerThird; }
    QObject* pit() const { return m_pit; }
    QObject* lights() const { return m_lights; }
    QObject* fastestLap() const { return m_fastestLap; }
    QObject* danger() const { return m_danger; }
    QObject* pitProjection() const { return m_pitProj; }
    QObject* trackmap() const { return m_trackmap; }
    QObject* charts() const { return m_charts; }
    QObject* championship() const { return m_champ; }
    bool layoutOffen() const { return m_layoutOffen.has_value(); }

    // ── Steuerung ────────────────────────────────────────────────────────────
    void start();
    void stop();
    void setBaseUrl(const QString& baseUrl);
    void setHz(int hz);
    /// Datenquelle fuer Nebendaten und den Layout-Rueckweg tauschen (lokal statt HTTP).
    void setDataSource(DataSource* source);
    /// Woher der LIVE-Strom kommt (Standard: SSE vom Server unter baseUrl). Der
    /// In-Process-Betrieb setzt hier den LocalFeed. Laeuft gerade kein Demo, wird
    /// sofort getauscht.
    using FeedFactory = std::function<Feed*(QObject* parent)>;
    void setLiveFeedFactory(FeedFactory factory);
    /// Waehrend des Layout-Bearbeitens erfundene Daten einspeisen.
    void setVorschau(bool on);

    // ── Fuer QML ─────────────────────────────────────────────────────────────
    /// Eine Einstellung lokal ueberschreiben. Ungueltige QVariant gibt sie zurueck.
    Q_INVOKABLE void override(const QString& key, const QVariant& value);
    /// Waehrend des Ziehens: nur lokal, damit der Baustein der Maus folgt.
    Q_INVOKABLE void layoutLive(const QVariant& layout);
    /// Nach dem Loslassen: an den Server schicken, erst in der Antwort freigeben.
    Q_INVOKABLE void layoutSpeichern(const QVariant& layout);

signals:
    /// Ein Zaehler, der bei jedem Payload hochgeht.
    void tickChanged();
    /// Wartet eine Layout-Aenderung noch darauf, zum Server zu kommen?
    void layoutOffenChanged(bool offen);

private:
    static QVariantMap alsMap(const QVariant& wert);
    void bindFeed(Feed* feed);
    Feed* makeLiveFeed();
    void onLink(bool linked);
    void onPayload(const QJsonObject& data);
    void updateParts(const QJsonObject& data, const QJsonObject& session,
                     const QList<QJsonObject>& drivers, bool connected, const DeriveResult& res);
    void updateRedFlag(const QJsonObject& data, double now);
    void updateSession(const QJsonObject& session, const QJsonObject& data, bool connected,
                       double now, const QList<QJsonObject>& drivers);
    void layoutMerken(const QVariantMap& daten);
    void layoutErledigt();
    void layoutGesendet(bool ok, const QJsonObject& antwort, const QString& fehler, const QVariantMap& daten);
    void szeneLeeren();
    static QVariantList tickerChips(const QJsonObject& session);

    Config m_cfg;
    SharedState m_shared;
    QString m_baseUrl;
    HttpDataSource* m_httpSource;
    DataSource* m_source;

    DriverModel* m_drivers;
    SessionState* m_session;
    SettingsState* m_settings;
    RegieState* m_regie;
    int m_tick = 0;

    MessageBanner* m_banner;
    RaceControl m_raceControl;
    Undercut m_undercut;
    Battles* m_battles;
    Hotlaps* m_hotlaps;
    Onboard* m_onboard;
    LowerThird* m_lowerThird;
    PitCards* m_pit;
    StartLights* m_lights;
    FastestLap* m_fastestLap;
    DangerZone* m_danger;
    PitProjection* m_pitProj;
    Trackmap* m_trackmap;
    Charts* m_charts;
    Championship* m_champ;

    bool m_everConnected = false;
    double m_lastConnMs = 0.0;
    std::optional<QVariantMap> m_layoutOffen;
    double m_redFlagUntil = 0.0;
    int m_rfLastRcId = 0;

    int m_hz;
    bool m_demoDauerhaft;       // per --demo gestartet
    bool m_demoAn;
    QString m_demoArt;
    Feed* m_feed = nullptr;
    FeedFactory m_liveFactory;
};

}  // namespace kers::hud
