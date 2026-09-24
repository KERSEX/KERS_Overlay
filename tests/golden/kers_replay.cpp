// kers_replay - eine .f1rec-Aufnahme durch den C++-Server schicken und den
// Zustand in festen Abstaenden als JSON-Zeilen ausgeben.
//
// Gegenstueck: golden_dump.py macht dasselbe mit den handle_*-Funktionen aus
// main.py. vergleich.py legt beide Ausgaben nebeneinander.
//
//   kers_replay <aufnahme.f1rec> <ausgabe.jsonl> [--every 5] [--builtin static/tracks.json]
//
// Die Uhr laeuft mit der Aufnahme (Startzeit + Zeitstempel des Pakets), damit
// connected / start_lights.age / race_control.t auf beiden Seiten gleich sind.
#include "payload.h"
#include "race_state.h"
#include "settings_store.h"
#include "tracks.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

#include <QtZlib/zlib.h>

#include <cstring>

using namespace kers::server;

namespace {

QByteArray gunzip(const QByteArray& in, bool* ok)
{
    *ok = false;
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK)          // gzip oder zlib, automatisch erkannt
        return {};
    QByteArray out;
    char buf[1 << 16];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    z.avail_in = static_cast<uInt>(in.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof(buf);
        rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, static_cast<int>(sizeof(buf) - z.avail_out));
        if (rc == Z_BUF_ERROR && z.avail_in == 0)
            break;                                    // Datei endet ohne Abschluss (Absturz beim Aufnehmen)
    }
    inflateEnd(&z);
    *ok = rc == Z_STREAM_END || rc == Z_BUF_ERROR;
    return out;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 3) {
        QTextStream(stderr) << "kers_replay <aufnahme.f1rec> <ausgabe.jsonl> [--every 5] [--builtin tracks.json]\n";
        return 2;
    }
    double every = 5.0;
    QString builtin = QStringLiteral("static/tracks.json");
    for (int i = 3; i + 1 < a.size(); ++i) {
        if (a.at(i) == QLatin1String("--every"))
            every = a.at(++i).toDouble();
        else if (a.at(i) == QLatin1String("--builtin"))
            builtin = a.at(++i);
    }

    QFile f(a.at(1));
    if (!f.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "nicht lesbar: " << a.at(1) << "\n";
        return 1;
    }
    bool ok = false;
    const QByteArray raw = gunzip(f.readAll(), &ok);
    if (!ok) {
        QTextStream(stderr) << "kein gzip: " << a.at(1) << "\n";
        return 1;
    }
    const int nl = raw.indexOf('\n');
    const QJsonObject meta = QJsonDocument::fromJson(raw.left(nl)).object();

    QTemporaryDir dir;
    SettingsStore settings(dir.path());
    TrackStore tracks(builtin, dir.filePath(QStringLiteral("tracks.json")));
    RaceState st(settings, tracks);
    const double t0 = 1.8e9;                         // feste Startzeit, wie in golden_dump.py
    double clock = t0;
    st.setClock([&clock] { return clock; });
    const QJsonObject regie{{"chart", "none"}, {"strat", false}, {"champ", false}, {"battles", true}, {"hotlap", true}};

    QFile out(a.at(2));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr) << "nicht schreibbar: " << a.at(2) << "\n";
        return 1;
    }
    int pos = nl + 1;
    int n = 0;
    double next = every;
    while (pos + 12 <= raw.size()) {
        double ts;
        quint32 len;
        std::memcpy(&ts, raw.constData() + pos, 8);
        std::memcpy(&len, raw.constData() + pos + 8, 4);
        pos += 12;
        if (pos + static_cast<int>(len) > raw.size())
            break;
        const QByteArray pkt = raw.mid(pos, static_cast<int>(len));
        pos += static_cast<int>(len);
        clock = t0 + ts;
        st.feed(pkt);
        ++n;
        if (ts >= next) {
            QJsonObject zeile{{"t", std::round(ts * 1000.0) / 1000.0},
                              {"live", buildLivePayload(st, settings.settings(), regie)},
                              {"track", trackJson(st)},
                              {"lap_positions", lapPositionsJson(st)}};
            out.write(QJsonDocument(zeile).toJson(QJsonDocument::Compact) + '\n');
            while (next <= ts)
                next += every;
        }
    }
    QTextStream(stdout) << n << " Pakete aus " << a.at(1) << " (" << meta.value("created").toString() << ")\n";
    return 0;
}
