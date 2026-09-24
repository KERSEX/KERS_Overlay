#include "recorder.h"

#include "constants.h"
#include "pyconv.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDebug>

#include <QtZlib/zlib.h>

#include <cmath>
#include <cstring>

namespace kers::server {

// ── GzipWriter ───────────────────────────────────────────────────────────────
struct GzipWriter::Impl {
    z_stream z{};
    unsigned char out[64 * 1024];
};

GzipWriter::GzipWriter() : m(std::make_unique<Impl>()) {}

GzipWriter::~GzipWriter()
{
    close();
}

bool GzipWriter::open(const QString& path)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly))
        return false;
    std::memset(&m->z, 0, sizeof(m->z));
    // windowBits 15 + 16 = gzip-Kopf und -Pruefsumme statt zlib-Rahmen
    if (deflateInit2(&m->z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        m_file.close();
        return false;
    }
    m_open = true;
    return true;
}

bool GzipWriter::pump(int flush)
{
    do {
        m->z.next_out = m->out;
        m->z.avail_out = sizeof(m->out);
        const int rc = deflate(&m->z, flush);
        if (rc == Z_STREAM_ERROR)
            return false;
        const qint64 n = static_cast<qint64>(sizeof(m->out) - m->z.avail_out);
        if (n > 0 && m_file.write(reinterpret_cast<const char*>(m->out), n) != n)
            return false;
        if (flush == Z_FINISH && rc == Z_STREAM_END)
            return true;
    } while (m->z.avail_out == 0 || (flush == Z_FINISH));
    return true;
}

bool GzipWriter::write(const QByteArray& data)
{
    if (!m_open || data.isEmpty())
        return m_open;
    m->z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    m->z.avail_in = static_cast<uInt>(data.size());
    const bool ok = pump(Z_NO_FLUSH);
    m->z.next_in = nullptr;
    m->z.avail_in = 0;
    return ok;
}

bool GzipWriter::close()
{
    if (!m_open)
        return true;
    m_open = false;
    const bool ok = pump(Z_FINISH);
    deflateEnd(&m->z);
    m_file.close();
    return ok;
}

// ── Recorder ─────────────────────────────────────────────────────────────────
const QSet<int>& Recorder::parsedPids()
{
    static const QSet<int> p = {0, 1, 2, 3, 4, 6, 7, 8, 10, 15, 16};
    return p;
}

Recorder::Recorder(QString dir) : m_dir(std::move(dir)) {}

Recorder::~Recorder()
{
    if (m_active)
        stop();
}

QJsonObject Recorder::start(const QString& name, bool onlyParsed, double now)
{
    if (m_active)
        return {{"ok", false}, {"err", QStringLiteral(u"läuft bereits")}};
    QDir().mkpath(m_dir);
    // Nur Buchstaben, Ziffern, Leerzeichen, _ und - ; hoechstens 40 Zeichen
    QString safe;
    for (const QChar c : name)
        if (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('-'))
            safe += c;
    safe = safe.left(40).trimmed().replace(QLatin1Char(' '), QLatin1Char('_'));
    const QDateTime jetzt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(now * 1000.0));
    const QString fn = jetzt.toString(QStringLiteral("yyyy-MM-dd_HHmmss")) + (safe.isEmpty() ? QString() : QLatin1Char('_') + safe)
        + QStringLiteral(".f1rec");
    const QString path = QDir(m_dir).filePath(fn);
    if (!m_gz.open(path))
        return {{"ok", false}, {"err", QStringLiteral("Datei nicht anlegbar: %1").arg(fn)}};
    // Kompakt geschrieben - json.loads in testgui.py liest das genauso
    const QJsonObject meta{{"created", jetzt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
                           {"format", EXPECTED_FORMAT}, {"only_parsed", onlyParsed}, {"note", name}};
    m_gz.write(QJsonDocument(meta).toJson(QJsonDocument::Compact) + '\n');
    m_active = true;
    m_onlyParsed = onlyParsed;
    m_t0 = now;
    m_count = 0;
    m_bytes = 0;
    m_path = path;
    qInfo().noquote() << "[REC] Aufnahme gestartet:" << fn;
    return {{"ok", true}, {"file", fn}};
}

QJsonObject Recorder::stop()
{
    if (!m_active)
        return {{"ok", false}, {"err", QStringLiteral(u"läuft nicht")}};
    m_gz.close();
    m_active = false;
    const QString fn = QFileInfo(m_path).fileName();
    qInfo().noquote() << "[REC] Aufnahme beendet:" << fn << m_count << "Pakete";
    return {{"ok", true}, {"file", fn}, {"count", m_count}, {"mb", py::round(m_bytes / 1e6, 2)}};
}

QJsonObject Recorder::status(double now) const
{
    return {{"active", m_active},
            {"file", m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName()},
            {"secs", m_active ? py::round(now - m_t0, 1) : 0.0},
            {"count", m_count},
            {"mb", py::round(m_bytes / 1e6, 2)}};
}

void Recorder::record(int packetId, const QByteArray& data, double now)
{
    if (!m_active || (m_onlyParsed && !parsedPids().contains(packetId)))
        return;
    QByteArray rec(12, '\0');
    const double rel = now - m_t0;
    const quint32 len = static_cast<quint32>(data.size());
    std::memcpy(rec.data(), &rel, 8);
    std::memcpy(rec.data() + 8, &len, 4);
    rec += data;
    if (m_gz.write(rec)) {
        ++m_count;
        m_bytes += data.size();
    }
}

}  // namespace kers::server
