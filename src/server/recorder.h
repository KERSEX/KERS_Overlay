// UDP-Recorder: rohe Pakete mitschneiden, fuer das Replay in Test\testgui.py.
//
// Format .f1rec = gzip: eine JSON-Metazeile, danach je Paket
//   <d  Sekunden seit Start (double)
//   <I  Laenge
//   Rohpaket
// Genau wie _rec_start/_rec_stop in main.py, damit testgui.py die Dateien beider
// Fassungen abspielen kann.
#pragma once

#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <memory>

namespace kers::server {

/// gzip-Datei schreiben (zlib aus Qt, deflate mit gzip-Kopf).
class GzipWriter {
public:
    GzipWriter();
    ~GzipWriter();
    GzipWriter(const GzipWriter&) = delete;
    GzipWriter& operator=(const GzipWriter&) = delete;

    bool open(const QString& path);
    bool write(const QByteArray& data);
    bool close();
    bool isOpen() const { return m_open; }

private:
    bool pump(int flush);
    struct Impl;
    std::unique_ptr<Impl> m;
    QFile m_file;
    bool m_open = false;
};

class Recorder {
public:
    /// Nur was das Overlay verarbeitet (only_parsed) - PID 11 ist bewusst NICHT dabei.
    static const QSet<int>& parsedPids();

    explicit Recorder(QString dir);
    ~Recorder();

    QJsonObject start(const QString& name, bool onlyParsed, double now);
    QJsonObject stop();
    QJsonObject status(double now) const;
    bool active() const { return m_active; }
    /// Ein Paket mitschreiben, falls aktiv und die PID durch den Filter passt.
    void record(int packetId, const QByteArray& data, double now);

private:
    QString m_dir;
    bool m_active = false;
    bool m_onlyParsed = true;
    double m_t0 = 0.0;
    qint64 m_count = 0;
    qint64 m_bytes = 0;
    QString m_path;
    GzipWriter m_gz;
};

}  // namespace kers::server
