#include "bridge.h"

#include "demo_feed.h"
#include "sse_feed.h"

#include <QJSValue>
#include <QJsonArray>
#include <QDebug>

#include <algorithm>

namespace kers::hud {

OverlayBridge::OverlayBridge(const QString& baseUrl, int hz, const QString& demo, QObject* parent)
    : QObject(parent)
    , m_cfg()
    , m_shared(m_cfg)
    , m_baseUrl(baseUrl)
    , m_httpSource(new HttpDataSource(baseUrl, this))
    , m_source(m_httpSource)
    , m_drivers(new DriverModel(this))
    , m_session(new SessionState(this))
    , m_settings(new SettingsState(this))
    , m_regie(new RegieState(this))
    , m_banner(new MessageBanner(this))
    , m_raceControl(m_banner)
    , m_undercut(m_banner)
    , m_battles(new Battles(m_cfg, this))
    , m_hotlaps(new Hotlaps(m_shared, m_cfg, this))
    , m_onboard(new Onboard(m_shared, m_cfg, this))
    , m_lowerThird(new LowerThird(m_shared, m_cfg, this))
    , m_pit(new PitCards(this))
    , m_lights(new StartLights(this))
    , m_fastestLap(new FastestLap(m_cfg, this))
    , m_danger(new DangerZone(this))
    , m_pitProj(new PitProjection(this))
    , m_trackmap(new Trackmap(m_cfg, m_httpSource, this))
    , m_charts(new Charts(m_httpSource, m_shared, this))
    , m_champ(new Championship(m_httpSource, this))
    , m_hz(hz)
    , m_demoDauerhaft(!demo.isEmpty())
    , m_demoAn(!demo.isEmpty())
    , m_demoArt(demo)
{
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
    // Der Demo-Feed hat dieselbe Schnittstelle wie der echte - der Rest der
    // Bruecke merkt keinen Unterschied.
    if (!demo.isEmpty())
        bindFeed(new DemoFeed(hz, demo == QLatin1String("quali"), false, this));
    else
        bindFeed(makeLiveFeed());
    // Vorgaben sofort anwenden, damit QML nicht auf den ersten Payload wartet.
    m_settings->apply(m_cfg);
}

OverlayBridge::~OverlayBridge()
{
    stop();
}

Feed* OverlayBridge::makeLiveFeed()
{
    if (m_liveFactory)
        return m_liveFactory(this);
    return new SseFeed(m_baseUrl, m_hz, this);
}

void OverlayBridge::setLiveFeedFactory(FeedFactory factory)
{
    m_liveFactory = std::move(factory);
    if (m_demoAn || !m_feed)
        return;                       // Demo/Vorschau laeuft - getauscht wird beim Zurueckschalten
    m_feed->stop();
    m_feed->disconnect(this);
    m_feed->deleteLater();
    bindFeed(makeLiveFeed());
}

void OverlayBridge::bindFeed(Feed* feed)
{
    m_feed = feed;
    connect(feed, &Feed::payload, this, &OverlayBridge::onPayload);
    connect(feed, &Feed::linkChanged, this, &OverlayBridge::onLink);
}

// ── Steuerung ────────────────────────────────────────────────────────────────
void OverlayBridge::start()
{
    m_feed->start();
    // Die Streckenkontur haengt nicht am Datenstrom. Im Demo-Modus liefert sie
    // der Feed selbst, sonst wird /api/track gepollt, bis die Strecke gelernt ist.
    if (m_feed->hasOwnExtras()) {
        m_trackmap->loadPoints(m_feed->trackPoints());
        m_champ->setDemoData(m_feed->championship());
    } else {
        m_trackmap->start();
    }
}

void OverlayBridge::stop()
{
    if (m_feed)
        m_feed->stop();
    m_trackmap->stop();
}

void OverlayBridge::setBaseUrl(const QString& baseUrl)
{
    m_baseUrl = baseUrl;
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
    m_feed->setBaseUrl(m_baseUrl);
    m_httpSource->setBaseUrl(m_baseUrl);
}

void OverlayBridge::setHz(int hz)
{
    m_hz = hz;
    m_feed->setHz(hz);
}

void OverlayBridge::setDataSource(DataSource* source)
{
    m_source = source ? source : m_httpSource;
    m_trackmap->setSource(m_source);
    m_charts->setSource(m_source);
    m_champ->setSource(m_source);
}

void OverlayBridge::override(const QString& key, const QVariant& value)
{
    // Eine Einstellung lokal ueberschreiben (Gegenstueck zu den URL-Parametern
    // der Web-Bausteine). `undefined`/null gibt sie an den Server zurueck.
    m_cfg.setOverride(key, value);
    m_settings->apply(m_cfg);
}

// ── Vorschau-Daten fuers Layout-Bearbeiten ───────────────────────────────────
void OverlayBridge::setVorschau(bool on)
{
    // ⚠ Wer das HUD mit --demo gestartet hat, bekommt hier nichts: dort laeuft
    // die Demo ohnehin, und ein Tausch wuerde sie nur neu starten.
    if (m_demoDauerhaft || on == m_demoAn)
        return;
    m_feed->stop();
    m_feed->disconnect(this);
    m_feed->deleteLater();
    m_feed = nullptr;

    if (on) {
        bindFeed(new DemoFeed(m_hz, false, true, this));
    } else {
        bindFeed(makeLiveFeed());
    }
    m_demoAn = on;
    if (on) {
        m_trackmap->stop();
        m_trackmap->loadPoints(m_feed->trackPoints(), 1000 + m_tick);
        m_champ->setDemoData(m_feed->championship());
    } else {
        // Die Ueberschreibung aus dem Bearbeiten aufgeben: ab jetzt liefert
        // wieder der Server die Settings. ⚠ NUR, wenn wirklich gespeichert
        // wurde - solange etwas offen ist, bleibt sie stehen, bis onLink sie
        // nachgereicht hat.
        if (!m_layoutOffen)
            m_cfg.setOverride(QStringLiteral("layout"), QVariant());
        m_champ->clearDemoData();
        szeneLeeren();
        m_trackmap->start();
    }
    m_feed->start();
}

void OverlayBridge::szeneLeeren()
{
    // Zurueck auf "noch nichts empfangen", nachdem die Vorschau aus ist. Ohne
    // das bleibt das erfundene Rennen stehen, bis ein Server wieder liefert.
    // Die abgeleitete Buchhaltung muss mit weg, sonst schleppte eine spaetere
    // echte Session die Bestsektoren und Stints des erfundenen Rennens mit.
    m_shared.resetAll();
    m_everConnected = false;
    m_lastConnMs = 0.0;
    m_drivers->clear();
    // Diese drei haengen an eigenen Uhren - ein leerer Stand erreicht sie nicht.
    m_banner->leeren();
    m_raceControl.leeren();
    m_undercut.leeren();
    // Durch den normalen Weg schicken, damit jeder Baustein genau so
    // abgeschaltet wird wie ohne Verbindung beim Start. Die Settings gehen als
    // aktueller Stand mit, sonst blitzten Branding und Deckkraft kurz auf.
    QJsonObject leer;
    leer["connected"] = false;
    leer["drivers"] = QJsonArray();
    leer["session"] = QJsonObject();
    leer["settings"] = QJsonObject::fromVariantMap(m_cfg.values());
    leer["regie"] = QJsonObject();
    onPayload(leer);
}

// ── Layout zurueckschreiben ──────────────────────────────────────────────────
QVariantMap OverlayBridge::alsMap(const QVariant& wert)
{
    // Ein in QML gebautes JS-Objekt kommt je nach Weg als QJSValue oder schon
    // als QVariantMap an - beides in eine echte Map umwandeln.
    if (wert.userType() == qMetaTypeId<QJSValue>())
        return wert.value<QJSValue>().toVariant().toMap();
    return wert.toMap();
}

void OverlayBridge::layoutLive(const QVariant& layout)
{
    // Lokale Ueberschreibungen gewinnen gegen den Server (Config::apply), der
    // laufende Datenstrom kann den Baustein also nicht zurueckspringen lassen.
    override(QStringLiteral("layout"), alsMap(layout));
}

void OverlayBridge::layoutSpeichern(const QVariant& layout)
{
    // ⚠ Reihenfolge: Ueberschreibung stehen lassen, senden, und erst in der
    // Antwort (die den kompletten neuen Stand enthaelt) aufgeben - sonst gaelte
    // bis zur Antwort der ALTE Serverwert und der Baustein spraenge kurz zurueck.
    const QVariantMap daten = alsMap(layout);
    override(QStringLiteral("layout"), daten);
    QJsonObject body;
    body["layout"] = QJsonObject::fromVariantMap(daten);
    m_source->post(QStringLiteral("/api/settings"), body,
                   [this, daten](bool ok, const QJsonObject& antwort, const QString& fehler) {
                       layoutGesendet(ok, antwort, fehler, daten);
                   });
}

void OverlayBridge::layoutMerken(const QVariantMap& daten)
{
    const bool vorher = m_layoutOffen.has_value();
    m_layoutOffen = daten;
    if (!vorher)
        emit layoutOffenChanged(true);
}

void OverlayBridge::layoutErledigt()
{
    if (m_layoutOffen) {
        m_layoutOffen.reset();
        emit layoutOffenChanged(false);
    }
}

void OverlayBridge::layoutGesendet(bool ok, const QJsonObject& antwort, const QString& fehler, const QVariantMap& daten)
{
    if (!ok) {
        // Nicht angekommen - der uebliche Fall ist ein Server, der (noch) nicht
        // laeuft. Merken: die Aenderung wartet auf den naechsten Kontakt (onLink).
        qWarning() << "[HUD] Layout nicht gespeichert:" << fehler;
        layoutMerken(daten);
        return;
    }
    layoutErledigt();
    // ⚠ Die Ueberschreibung darf NICHT aufgegeben werden, solange die Vorschau
    // laeuft: der Demo-Feed schickt in jedem Takt seinen Settings-Schnappschuss
    // von vor dem Bearbeiten mit. Beim Verlassen der Vorschau wird sie in
    // setVorschau aufgegeben.
    if (m_demoAn)
        m_cfg.setOverride(QStringLiteral("layout"), antwort.value(QLatin1String("layout")).toObject().toVariantMap());
    else
        m_cfg.setOverride(QStringLiteral("layout"), QVariant());   // ab jetzt gilt der Server
    m_cfg.apply(antwort);
    m_settings->apply(m_cfg);
}

// ── Payload verarbeiten ──────────────────────────────────────────────────────
void OverlayBridge::onLink(bool linked)
{
    m_session->set_linked(linked);
    // Wieder Kontakt: eine vorgemerkte Layout-Aenderung jetzt nachreichen.
    if (linked && m_layoutOffen)
        layoutSpeichern(*m_layoutOffen);
}

void OverlayBridge::onPayload(const QJsonObject& dataIn)
{
    const double now = m_shared.nowMs();

    m_cfg.apply(dataIn.value(QLatin1String("settings")).toObject());
    m_settings->apply(m_cfg);
    m_regie->apply(dataIn.value(QLatin1String("regie")).toObject());

    const QJsonObject data = m_shared.maybeHoldQuali(dataIn);
    const DeriveResult res = m_shared.derive(data);

    const QJsonObject session = data.value(QLatin1String("session")).toObject();
    const bool connected = jb(data, "connected");
    if (connected) {
        m_everConnected = true;
        m_lastConnMs = now;
    }

    updateRedFlag(data, now);
    updateSession(session, data, connected, now, res.drivers);
    m_drivers->update(res.drivers, m_shared, session, ji(data, "focus_index", -1), m_cfg);
    updateParts(data, session, res.drivers, connected, res);

    ++m_tick;
    emit tickChanged();
}

void OverlayBridge::updateParts(const QJsonObject& data, const QJsonObject& session,
                                const QList<QJsonObject>& drivers, bool connected, const DeriveResult& res)
{
    // Die Bedingungen sind dieselben wie in den jeweiligen JS-Dateien - dort am
    // KERS.onData-Hook ganz unten, hier gebuendelt an einer Stelle.
    const double wall = wallMs();
    const bool isQuali = jb(session, "is_quali");
    const int lap = ji(session, "current_lap");
    const int focus = ji(data, "focus_index", -1);
    const bool qualiResult = jb(session, "_quali_result");

    // Meldungen: Rennleitung und Undercut teilen sich denselben Banner.
    m_banner->setEnabled(m_cfg.b("msgs"));
    m_raceControl.update(data);
    if (res.scChanged)
        m_raceControl.onSafetyCar(res.scNow, res.scPrev);
    m_undercut.update(drivers, session, connected, m_cfg.b("undercut"));

    // Battle-Boxen: Regie-Schalter, Setting, verbunden, kein Quali, ab Runde 3.
    m_battles->update(drivers, data, session,
                      m_regie->battles() && m_cfg.b("battles") && connected && !isQuali && lap >= 3);
    // Hotlap-Boxen: Regie-Schalter, verbunden, Quali, kein eingefrorenes Ergebnis.
    m_hotlaps->update(drivers, focus, m_regie->hotlap() && connected && isQuali && !qualiResult);

    m_onboard->update(drivers, focus, connected, session, m_cfg.b("onboard"));
    m_lowerThird->update(drivers, focus, session, connected, m_cfg.b("lowerthird"));
    m_pit->update(drivers, session, connected);
    m_lights->update(data.value(QLatin1String("start_lights")).toObject(), connected, m_cfg.b("lights"));
    m_fastestLap->update(session, connected, m_cfg.b("flbanner"));
    m_danger->update(drivers, session, connected, m_cfg.b("danger"));
    m_pitProj->update(drivers, focus, session, connected, m_cfg.b("pitproj"));
    m_trackmap->update(drivers, focus, connected, session, m_cfg.b("map"), wall);
    m_charts->update(m_regie->chart(), wall);
    m_champ->update(m_regie->champ(), wall, m_cfg.s("champ_view"));
}

void OverlayBridge::updateRedFlag(const QJsonObject& data, double now)
{
    // Rote Flagge aus dem race_control-Feed (tower.js, gleiche Logik).
    const QJsonArray rc = data.value(QLatin1String("race_control")).toArray();
    if (!rc.isEmpty() && ji(rc.last().toObject(), "id") < m_rfLastRcId)
        m_rfLastRcId = 0;
    for (const QJsonValue& v : rc) {
        const QJsonObject msg = v.toObject();
        const int id = ji(msg, "id");
        if (id <= m_rfLastRcId)
            continue;
        m_rfLastRcId = id;
        if (js(msg, "type") == QLatin1String("redflag"))
            m_redFlagUntil = now + RED_FLAG_MS;
    }
}

void OverlayBridge::updateSession(const QJsonObject& session, const QJsonObject& data, bool connected,
                                  double now, const QList<QJsonObject>& drivers)
{
    // Die Kopfzeile - Portierung des oberen Teils von renderTower().
    Q_UNUSED(data);
    SessionState* st = m_session;
    const bool isQuali = jb(session, "is_quali");

    // Strecke: kommt fertig vom Server (TRACK_INFO) - keine zweite Namensliste hier.
    st->set_trackName(js(session, "track_name"));
    st->set_trackColors(session.value(QLatin1String("track_colors")).toArray().toVariantList());

    // F1 26 hat statt DRS den "Overtake Mode" -> Spaltenkopf heisst dann MOM.
    const bool isF126 = ji(session, "formula") == 13 || js(session, "formula_name").contains(QLatin1String("26"));
    const QString boost = isF126 ? QStringLiteral("MOM") : QStringLiteral("DRS");

    QString title, subtitle;
    if (isQuali) {
        const QString typeName = js(session, "type_name");
        title = (!typeName.isEmpty() && typeName != QLatin1String("Unbekannt")) ? typeName.toUpper() : st->title();
        const int left = ji(session, "time_left");
        subtitle = left > 0 ? QStringLiteral("%1:%2").arg(left / 60).arg(left % 60, 2, 10, QLatin1Char('0')) : QString();
    } else {
        // Im Rennen steht nur "Runde X / Y" - kein Wort "RENNEN".
        const int total = ji(session, "total_laps");
        subtitle = total > 0 ? QStringLiteral("Runde %1 / %2").arg(ji(session, "current_lap")).arg(total) : QString();
    }

    const QString scStatus = connected ? js(session, "safety_car_status", QStringLiteral("none")) : QStringLiteral("none");
    const int totalLaps = ji(session, "total_laps");
    const bool finalLap = !isQuali && totalLaps > 0 && ji(session, "current_lap") >= totalLaps;
    QString flagText, flagKind;
    if (jb(session, "_quali_result")) {
        flagText = QStringLiteral(u"\U0001F3C1 Ergebnis");
        flagKind = QStringLiteral("fin");
    } else if (scStatus == QLatin1String("sc")) {
        flagText = QStringLiteral("Safety Car");
        flagKind = QStringLiteral("sc");
    } else if (scStatus == QLatin1String("vsc")) {
        flagText = QStringLiteral("Virtual SC");
        flagKind = QStringLiteral("sc");
    } else if (connected && finalLap) {
        flagText = QStringLiteral(u"\U0001F3C1 Letzte Runde");
        flagKind = QStringLiteral("fin");
    }

    // Quali: Pole-Zeit und Elimination-Schnitt
    double pole = 0.0;
    QString poleName;
    int elimCut = 0;
    if (isQuali) {
        for (const QJsonObject& d : drivers) {
            const double b = jd(d, "best_lap");
            if (b > 0 && (pole == 0.0 || b < pole)) {
                pole = b;
                poleName = js(d, "name");
            }
        }
        const int type = ji(session, "type");
        elimCut = type == 5 ? 16 : (type == 6 ? 10 : 0);
    }

    // "Warte auf Telemetrie" nur, wenn NOCH NIE Daten kamen oder das letzte
    // Ergebnis laenger als `holds` her ist.
    const int holds = m_cfg.i("holds") > 0 ? m_cfg.i("holds") : 300;
    const bool hold = m_everConnected && !connected && (now - m_lastConnMs) / 1000.0 < holds;

    st->set_connected(connected);
    st->set_holdResult(hold);
    st->set_isQuali(isQuali);
    st->set_sessionName(js(session, "type_name"));
    st->set_currentLap(ji(session, "current_lap"));
    st->set_totalLaps(totalLaps);
    st->set_timeLeft(ji(session, "time_left"));
    st->set_title(title);
    st->set_subtitle(subtitle);
    st->set_flagText(flagText);
    st->set_flagKind(flagKind);
    st->set_boostLabel(boost);
    st->set_headLeader(isQuali ? QStringLiteral("BEST") : QStringLiteral("LEADER"));
    st->set_headInterval(isQuali ? QStringLiteral("GAP") : QStringLiteral("INTERVAL"));
    st->set_headRight(isQuali ? QStringLiteral("STATUS") : boost);
    st->set_scStatus(scStatus);
    st->set_redFlag(connected && now < m_redFlagUntil);
    st->set_poleTime(pole);
    st->set_poleName(poleName);
    st->set_elimCut(elimCut);
    st->set_ticker(tickerChips(session));
}

QVariantList OverlayBridge::tickerChips(const QJsonObject& session)
{
    // Wetter-Chips fuer den Ticker im Tower-Kopf (tower.js buildTickerItems):
    // Jetzt-Wert plus bis zu vier Vorhersagen.
    QVariantList chips;
    if (js(session, "weather_name").isEmpty())
        return chips;
    QVariantMap now;
    now.insert("label", QStringLiteral("Jetzt"));
    now.insert("emoji", js(session, "weather_emoji"));
    now.insert("rain", ji(session, "weather_rain"));
    now.insert("now", true);
    chips.append(now);
    const QJsonArray fc = session.value(QLatin1String("forecast")).toArray();
    for (int i = 0; i < fc.size() && i < 4; ++i) {
        const QJsonObject f = fc.at(i).toObject();
        QVariantMap c;
        c.insert("label", QStringLiteral("+%1 Min").arg(ji(f, "t")));
        c.insert("emoji", js(f, "emoji"));
        c.insert("rain", ji(f, "rain"));
        c.insert("now", false);
        chips.append(c);
    }
    return chips;
}

}  // namespace kers::hud
