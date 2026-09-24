#include "kers_server.h"

#include "constants.h"
#include "packets.h"
#include "payload.h"
#include "pyconv.h"

#include <QDateTime>
#include <QDir>
#include <QNetworkDatagram>
#include <QDebug>

namespace kers::server {

namespace {

double wallSec()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QJsonObject regieStart()
{
    return {{"chart", "none"}, {"strat", false}, {"champ", false}, {"battles", true}, {"hotlap", true}};
}

}  // namespace

KersServer::KersServer(const QString& dataDir, const QString& builtinTracks, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_clock(&wallSec)
    , m_settings(dataDir)
    , m_tracks(builtinTracks, QDir(dataDir).filePath(QStringLiteral("tracks.json")))
    , m_regie(regieStart())
    , m_recorder(QDir(dataDir).filePath(QStringLiteral("recordings")))
    , m_http([this](const HttpRequest& r) { return handle(r); })
    , m_sse([this] { return livePayload(); }, [this] { return m_state->lastPacketTime(); }, [this] { return now(); })
{
    m_state = std::make_unique<RaceState>(m_settings, m_tracks);
    m_state->setClock(m_clock);
    connect(&m_udp, &QUdpSocket::readyRead, this, &KersServer::onUdp);
}

KersServer::~KersServer()
{
    stop();
}

void KersServer::setClock(std::function<double()> clock)
{
    m_clock = std::move(clock);
    m_state->setClock(m_clock);
}

void KersServer::neuerZustand()
{
    // Wie ein Neustart von main.py: frischer Rennzustand, Settings und Strecken bleiben.
    m_state = std::make_unique<RaceState>(m_settings, m_tracks);
    m_state->setClock(m_clock);
    m_regie = regieStart();
}

bool KersServer::start(quint16 httpPort, quint16 udpPort, const QHostAddress& bind)
{
    if (m_running)
        return true;
    m_error.clear();
    neuerZustand();
    if (!m_http.listen(bind, httpPort)) {
        m_error = QStringLiteral("HTTP-Port %1 belegt oder gesperrt: %2").arg(httpPort).arg(m_http.errorString());
        qWarning().noquote() << "[HTTP]" << m_error;
        return false;
    }
    // ⚠ DontShareAddress (= SO_EXCLUSIVEADDRUSE): nur EINE Quelle auf dem UDP-Port.
    // Mit SO_REUSEADDR (wie main.py) koennten sich zwei Server denselben Port teilen,
    // und Windows verteilt die Pakete dann unberechenbar auf beide.
    if (!m_udp.bind(bind, udpPort, QAbstractSocket::DontShareAddress)) {
        m_error = QStringLiteral("UDP-Port %1 belegt: %2").arg(udpPort).arg(m_udp.errorString());
        qWarning().noquote() << "[UDP]" << m_error;
        m_http.close();
        return false;
    }
    // Grosser Empfangspuffer: haengt die Oberflaeche kurz, gehen keine Pakete verloren
    m_udp.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1 << 20);
    m_httpPort = m_http.port();
    m_udpPort = m_udp.localPort();
    m_running = true;
    qInfo().noquote() << QStringLiteral("[HTTP] Server laeuft auf Port %1").arg(m_httpPort);
    qInfo().noquote() << QStringLiteral("[UDP] Listening on :%1 (F1 26 / Format 2026)").arg(m_udpPort);
    emit runningChanged(true);
    return true;
}

void KersServer::stop()
{
    if (!m_running)
        return;
    m_sse.closeAll();
    m_http.close();
    m_udp.close();
    if (m_recorder.active())
        m_recorder.stop();
    m_running = false;
    qInfo() << "[HTTP] Server gestoppt";
    emit runningChanged(false);
}

void KersServer::onUdp()
{
    while (m_udp.hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udp.receiveDatagram(8192);
        if (dg.isValid())
            feedPacket(dg.data());
    }
}

void KersServer::feedPacket(const QByteArray& data)
{
    const PacketHeader h = parseHeader(data);
    if (!h.ok)
        return;
    m_recorder.record(h.packetId, data, now());
    m_state->feed(data);
    emit packetReceived();
}

QJsonObject KersServer::livePayload()
{
    return buildLivePayload(*m_state, m_settings.settings(), m_regie);
}

QJsonObject KersServer::trackJson()
{
    return server::trackJson(*m_state);
}

QJsonObject KersServer::lapPositions() const
{
    return lapPositionsJson(*m_state);
}

QJsonObject KersServer::championship() const
{
    return calculateChampionship(*m_state, m_settings.champPath());
}

QJsonObject KersServer::status() const
{
    return {{"connected", m_state->connected()}, {"driver_count", m_state->drivers().size()}};
}

QJsonObject KersServer::applySettings(const QJsonObject& patch)
{
    const bool ausrichtung = m_settings.apply(patch);
    // Drehung oder Spiegelung verstellt? Dann gehoert das zur LAUFENDEN Strecke.
    const SessionInfo& s = m_state->session();
    if (ausrichtung && s.has_track_id) {
        const QJsonObject& st = m_settings.settings();
        const int rot = static_cast<int>(py::toInt(st.value("maprot")).value_or(0));
        m_tracks.ausrichtungMerken(s.track_id, rot, py::truthy(st.value("mapflip")));
    }
    emit settingsChanged();
    return m_settings.settings();
}

void KersServer::relearn()
{
    // ⚠ Die GESPEICHERTE muss mit weg, sonst laedt sie sich beim naechsten
    // Streckenwechsel sofort wieder und der Knopf tut scheinbar nichts.
    const SessionInfo& s = m_state->session();
    if (s.has_track_id)
        m_tracks.loeschen(s.track_id);
    m_state->learner().neuLernen();
}

}  // namespace kers::server
