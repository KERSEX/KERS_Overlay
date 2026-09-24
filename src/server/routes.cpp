// Die Routen - Portierung der @app.route-Funktionen aus main.py.
//
// Antworten haben dieselbe JSON-Form wie in 0.2.6: regie.html, settings.html,
// das Web-Overlay und das Python-HUD (--url) haengen daran.
#include "kers_server.h"

#include "constants.h"
#include "payload.h"
#include "pyconv.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

namespace kers::server {

namespace {

// Einzelne Overlay-Bausteine als eigene Seiten (fuer OBS). Feste Liste:
// kein Pfad, kein "..", nichts ausserhalb von templates/parts/.
const QStringList PARTS = {"battles", "champ", "charts", "danger", "flbanner", "hotlap", "lights", "lowerthird",
                           "onboard", "pit", "pitproj", "racemsg", "tower", "trackmap", "undercut"};

struct VersionedTemplate {
    const char* file;
    const char* label;
};
const VersionedTemplate VERSIONED_TEMPLATES[] = {
    {"index.html", "Overlay (Produktion)"},
    {"test.html", "Overlay (Spielwiese)"},
    {"testopti.html", "Overlay (Sparfassung)"},
    {"regie.html", "Regie-Panel"},
    {"settings.html", "Diese Seite"},
};

QByteArray lesen(const QString& pfad, bool* ok = nullptr)
{
    QFile f(pfad);
    const bool offen = f.open(QIODevice::ReadOnly);
    if (ok)
        *ok = offen;
    return offen ? f.readAll() : QByteArray();
}

bool istPost(const HttpRequest& r) { return r.method == "POST"; }

}  // namespace

HttpResponse KersServer::page(const QString& templateName) const
{
    bool ok = false;
    const QByteArray html = lesen(QStringLiteral(":/templates/") + templateName, &ok);
    return ok ? HttpResponse::html(html) : HttpResponse::error(404);
}

HttpResponse KersServer::partsIndex() const
{
    // Der einzige Jinja-Block im ganzen Projekt: {% for p in parts %} ... {% endfor %}
    // mit {{ p }} darin. Statt einer Template-Engine eine Ersetzung.
    bool ok = false;
    QByteArray html = lesen(QStringLiteral(":/templates/parts_index.html"), &ok);
    if (!ok)
        return HttpResponse::error(404);
    const QByteArray start = "{% for p in parts %}";
    const QByteArray ende = "{% endfor %}";
    const int a = html.indexOf(start);
    const int b = html.indexOf(ende, a);
    if (a >= 0 && b > a) {
        const QByteArray koerper = html.mid(a + start.size(), b - a - start.size());
        QByteArray raus;
        for (const QString& p : PARTS) {
            QByteArray k = koerper;
            k.replace("{{ p }}", p.toUtf8());
            raus += k;
        }
        html = html.left(a) + raus + html.mid(b + ende.size());
    }
    return HttpResponse::html(html);
}

HttpResponse KersServer::staticFile(const HttpRequest& req, const QString& rel) const
{
    // Wie safe_join: kein Weg aus static/ heraus
    const QString clean = QDir::cleanPath(rel);
    if (clean.isEmpty() || clean.startsWith(QLatin1String("..")) || clean.startsWith(QLatin1Char('/'))
        || clean.contains(QLatin1String("/../")) || clean == QLatin1String("."))
        return HttpResponse::error(404);
    const QString pfad = QStringLiteral(":/static/") + clean;
    QFileInfo info(pfad);
    if (!info.exists() || info.isDir())
        return HttpResponse::error(404);
    bool ok = false;
    HttpResponse r;
    r.body = lesen(pfad, &ok);
    if (!ok)
        return HttpResponse::error(404);
    r.contentType = mimeFor(clean);
    // Wie SEND_FILE_MAX_AGE_DEFAULT = 0: jedes Mal nachfragen, aber mit ETag -
    // unveraendert gibt es ein kurzes 304 statt der ganzen Datei.
    const QByteArray etag = '"' + QCryptographicHash::hash(r.body, QCryptographicHash::Sha1).toHex().left(16) + '"';
    r.addHeader("Cache-Control", "no-cache");
    r.addHeader("ETag", etag);
    if (req.header("if-none-match") == etag) {
        r.status = 304;
        r.body.clear();
    }
    return r;
}

QJsonObject KersServer::apiVersion() const
{
    // Kompiliert: nichts kann sich zur Laufzeit aendern, ein Neustart steht nie aus.
    // "frozen" sagt settings.html, dass die Dateiliste nur Rauschen waere.
    static const QRegularExpression verRe(QStringLiteral("KERS-VERSION:\\s*([0-9]+\\.[0-9]+\\.[0-9]+)"));
    QJsonArray files;
    files.append(QJsonObject{{"file", "KERS_Subsystems.exe"}, {"label", "Server / Backend"},
                             {"version", QString::fromLatin1(KERS_VERSION_STR)}, {"changed", QJsonValue::Null},
                             {"hash", QJsonValue::Null}, {"hash_disk", QJsonValue::Null},
                             {"restart_needed", false}, {"note", QJsonValue::Null}});
    for (const VersionedTemplate& t : VERSIONED_TEMPLATES) {
        bool ok = false;
        const QByteArray raw = lesen(QStringLiteral(":/templates/") + QLatin1String(t.file), &ok);
        QJsonObject info{{"file", t.file}, {"label", t.label}, {"restart_needed", false}};
        if (!ok) {
            info["version"] = QJsonValue::Null;
            info["note"] = QStringLiteral("Datei nicht gefunden");
        } else {
            const auto m = verRe.match(QString::fromUtf8(raw.left(4000)));
            info["version"] = m.hasMatch() ? QJsonValue(m.captured(1)) : QJsonValue::Null;
            info["changed"] = QJsonValue::Null;
            info["hash"] = QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex().left(8));
            info["note"] = m.hasMatch() ? QJsonValue::Null : QJsonValue(QStringLiteral("kein Versions-Marker in der Datei"));
        }
        files.append(info);
    }
    QFile vf(QStringLiteral(":/static/version.txt"));
    QString app = QString::fromLatin1(KERS_VERSION_STR);
    if (vf.open(QIODevice::ReadOnly)) {
        const QString v = QString::fromUtf8(vf.readAll()).trimmed();
        if (!v.isEmpty())
            app = v;
    }
    return {{"app", app}, {"frozen", true}, {"restart_needed", false}, {"files", files}};
}

HttpResponse KersServer::apiCam(const HttpRequest& req)
{
    // ⚠ Der Rumpf traegt NUR eine Positionsnummer. Welche Tasten das sind, steht
    // in CamKeys::folgeFuer - eine Taste aus dem Request wird nirgends angenommen.
    const QJsonObject d = req.jsonObject();
    const auto pos = py::toInt(d.value("pos"));
    if (!pos)
        return HttpResponse::json({{"error", "pos fehlt"}}, 400);
    if (*pos < 1 || *pos > CAM_MAX)
        return HttpResponse::json({{"error", QStringLiteral("pos muss zwischen 1 und %1 liegen").arg(CAM_MAX)}}, 400);
    const QJsonObject& st = m_settings.settings();
    const bool an = py::truthy(st.value("camkeys"));
    const QString weiter = st.value("camnextkey").toString();
    const long long haltenRoh = py::truthy(st.value("camholdms")) ? py::toInt(st.value("camholdms")).value_or(60) : 60;
    const int halten = static_cast<int>(haltenRoh);
    if (!an)
        return HttpResponse::json({{"error", "Kamera-Tasten sind in den Einstellungen aus"}}, 403);
    const QStringList folge = CamKeys::folgeFuer(static_cast<int>(*pos), weiter);
    if (folge.isEmpty())
        return HttpResponse::json({{"error", "Fuer P21/P22 fehlt die Taste 'naechster Fahrer' in den Einstellungen"}}, 400);
    const QString fehler = m_cam.druecke(folge, halten);
    if (!fehler.isEmpty()) {
        qWarning().noquote() << "[CAM] Tastendruck fehlgeschlagen:" << fehler;
        return HttpResponse::json({{"error", QStringLiteral("Tastendruck fehlgeschlagen: %1").arg(fehler)}}, 500);
    }
    return HttpResponse::json({{"pos", *pos}, {"keys", QJsonArray::fromStringList(folge)}, {"hold", halten}});
}

HttpResponse KersServer::apiTracks(const HttpRequest& req)
{
    // POST {"id": 11, "delete": true} loescht NUR aus der eigenen Datei. Eine
    // mitgelieferte Kontur steckt in der EXE; nach dem Loeschen gilt wieder sie.
    if (istPost(req)) {
        const QJsonObject d = req.jsonObject();
        const auto tid = py::toInt(d.value("id"));
        if (!tid)
            return HttpResponse::json({{"error", "id fehlt"}}, 400);
        const int id = static_cast<int>(*tid);
        if (py::truthy(d.value("delete")) && m_tracks.loeschen(id)) {
            // Laeuft gerade genau diese Strecke, sofort neu bestimmen - sonst bleibt
            // die geloeschte Karte bis zum naechsten Wechsel stehen.
            const SessionInfo& s = m_state->session();
            if (s.has_track_id && s.track_id == id)
                m_state->learner().streckeGewechselt(id, s.track_length);
        }
    }
    return HttpResponse::json(tracksJson(*m_state));
}

HttpResponse KersServer::apiPresets(const HttpRequest& req)
{
    // POST {"name": "Rennen"}                 -> aktuellen Stand sichern (gleicher Name = ueberschreiben)
    // POST {"name": "Rennen", "delete": true} -> loeschen
    if (istPost(req)) {
        const QJsonObject d = req.jsonObject();
        const QString name = (py::truthy(d.value("name")) ? py::toStr(d.value("name")) : QString()).trimmed().left(PRESET_NAME_MAX);
        if (name.isEmpty())
            return HttpResponse::json({{"error", "name fehlt"}, {"presets", m_settings.presets()}}, 400);
        if (py::truthy(d.value("delete"))) {
            m_settings.presetLoeschen(name);
        } else {
            const QString fehler = m_settings.presetSichern(name);
            if (!fehler.isEmpty())
                return HttpResponse::json({{"error", fehler}, {"presets", m_settings.presets()}}, 400);
        }
    }
    return HttpResponse::json({{"presets", m_settings.presets()}});
}

HttpResponse KersServer::apiLayouts(const HttpRequest& req)
{
    // ⚠ Nicht mit /api/layout (Einzahl) verwechseln - das liefert die Bausteinliste.
    if (istPost(req)) {
        const QJsonObject d = req.jsonObject();
        const QString name = (py::truthy(d.value("name")) ? py::toStr(d.value("name")) : QString()).trimmed().left(LAYOUT_NAME_MAX);
        if (name.isEmpty())
            return HttpResponse::json({{"error", "name fehlt"}, {"layouts", m_settings.layouts()}}, 400);
        if (py::truthy(d.value("delete"))) {
            m_settings.layoutLoeschen(name);
        } else {
            const QString fehler = m_settings.layoutSichern(name);
            if (!fehler.isEmpty())
                return HttpResponse::json({{"error", fehler}, {"layouts", m_settings.layouts()}}, 400);
        }
    }
    return HttpResponse::json({{"layouts", m_settings.layouts()}});
}

HttpResponse KersServer::handle(const HttpRequest& req)
{
    const QString& p = req.path;
    // Welche Methoden eine Route kann. Flask beantwortet OPTIONS selbst und
    // meldet 405 mit "Allow", wenn die Methode nicht passt.
    auto nur = [&](std::initializer_list<const char*> erlaubt) -> std::optional<HttpResponse> {
        QByteArray allow;
        bool passt = false;
        for (const char* m : erlaubt) {
            if (req.method == m)
                passt = true;
            allow += (allow.isEmpty() ? "" : ", ") + QByteArray(m);
        }
        if (allow.contains("GET"))
            allow += ", HEAD";
        allow += ", OPTIONS";
        if (req.method == "OPTIONS") {
            HttpResponse r;
            r.contentType = "text/html; charset=utf-8";
            r.addHeader("Allow", allow);
            return r;
        }
        if (!passt)
            return HttpResponse::methodNotAllowed(allow);
        return std::nullopt;
    };
#define KERS_METHODS(...)                   \
    if (auto r = nur({__VA_ARGS__}))        \
        return *r;

    // ── Seiten ───────────────────────────────────────────────────────────────
    if (p == QLatin1String("/")) { KERS_METHODS("GET") return page("index.html"); }
    if (p == QLatin1String("/test")) { KERS_METHODS("GET") return page("test.html"); }
    if (p == QLatin1String("/opti")) { KERS_METHODS("GET") return page("testopti.html"); }
    if (p == QLatin1String("/regie")) { KERS_METHODS("GET") return page("regie.html"); }
    if (p == QLatin1String("/settings")) { KERS_METHODS("GET") return page("settings.html"); }
    if (p == QLatin1String("/parts")) { KERS_METHODS("GET") return partsIndex(); }
    if (p.startsWith(QLatin1String("/part/"))) {
        const QString name = p.mid(6);
        if (!PARTS.contains(name))
            return HttpResponse::error(404);
        KERS_METHODS("GET")
        return page(QStringLiteral("parts/%1.html").arg(name));
    }
    if (p.startsWith(QLatin1String("/static/"))) {
        KERS_METHODS("GET")
        return staticFile(req, p.mid(8));
    }

    // ── API ──────────────────────────────────────────────────────────────────
    if (p == QLatin1String("/api/live")) { KERS_METHODS("GET") return HttpResponse::json(livePayload()); }
    if (p == QLatin1String("/api/stream")) {
        KERS_METHODS("GET")
        const double tick = SseHub::tickFor(req.arg(QStringLiteral("hz")));
        const bool slim = req.arg(QStringLiteral("slim")) == QLatin1String("1");
        return HttpResponse::eventStream([this, tick, slim](HttpConnection* c) { m_sse.add(c, tick, slim); });
    }
    if (p == QLatin1String("/api/status")) { KERS_METHODS("GET") return HttpResponse::json(status()); }
    if (p == QLatin1String("/api/track")) { KERS_METHODS("GET") return HttpResponse::json(trackJson()); }
    if (p == QLatin1String("/api/tracks")) { KERS_METHODS("GET", "POST") return apiTracks(req); }
    if (p == QLatin1String("/api/track/relearn")) {
        KERS_METHODS("GET", "POST")
        relearn();
        return HttpResponse::json({{"ok", true}});
    }
    if (p == QLatin1String("/api/lap_positions")) { KERS_METHODS("GET") return HttpResponse::json(lapPositions()); }
    if (p == QLatin1String("/api/championship")) { KERS_METHODS("GET") return HttpResponse::json(championship()); }
    if (p == QLatin1String("/api/regie")) {
        KERS_METHODS("GET", "POST")
        if (istPost(req)) {
            const QJsonObject d = req.jsonObject();
            const QString chart = d.value("chart").toString();
            if (d.value("chart").isString() && QStringList{"none", "pos", "gap", "lap"}.contains(chart))
                m_regie["chart"] = chart;
            for (const char* k : {"strat", "champ", "battles", "hotlap"})
                if (d.contains(QLatin1String(k)))
                    m_regie[QLatin1String(k)] = py::truthy(d.value(QLatin1String(k)));
            emit settingsChanged();
        }
        return HttpResponse::json(m_regie);
    }
    if (p == QLatin1String("/api/cam")) { KERS_METHODS("POST") return apiCam(req); }
    if (p == QLatin1String("/api/settings")) {
        KERS_METHODS("GET", "POST")
        if (istPost(req))
            return HttpResponse::json(applySettings(req.jsonObject()));
        return HttpResponse::json(m_settings.settings());
    }
    if (p == QLatin1String("/api/settings/reset")) {
        KERS_METHODS("POST")
        m_settings.reset();
        emit settingsChanged();
        return HttpResponse::json(m_settings.settings());
    }
    if (p == QLatin1String("/api/presets")) { KERS_METHODS("GET", "POST") return apiPresets(req); }
    if (p == QLatin1String("/api/layouts")) { KERS_METHODS("GET", "POST") return apiLayouts(req); }
    if (p == QLatin1String("/api/layout")) {
        KERS_METHODS("GET")
        QJsonArray teile;
        for (const LayoutTeil& t : LAYOUT_TEILE)
            teile.append(QJsonObject{{"key", t.key}, {"label", t.label}});
        return HttpResponse::json({{"teile", teile}, {"ecken", QJsonArray::fromStringList(LAYOUT_ECKEN)},
                                   {"standard", SettingsStore::layoutStandard()}});
    }
    if (p == QLatin1String("/api/logos")) {
        KERS_METHODS("GET")
        return HttpResponse::json({{"logos", QJsonArray::fromStringList(SettingsStore::listLogos())}});
    }
    if (p == QLatin1String("/api/champfiles")) {
        KERS_METHODS("GET")
        const QString cur = m_settings.settings().value("champ_file").toString();
        return HttpResponse::json({{"files", QJsonArray::fromStringList(m_settings.listChampFiles())},
                                   {"current", cur},
                                   {"dir", QDir::toNativeSeparators(QDir(m_dataDir).absolutePath())},
                                   {"default_exists", m_settings.defaultChampExists()}});
    }
    if (p == QLatin1String("/api/version")) { KERS_METHODS("GET") return HttpResponse::json(apiVersion()); }
    if (p == QLatin1String("/api/record/start")) {
        KERS_METHODS("POST")
        const QJsonObject d = req.jsonObject();
        const QString name = py::truthy(d.value("name")) ? py::toStr(d.value("name")) : QString();
        const bool onlyParsed = d.contains("only_parsed") ? py::truthy(d.value("only_parsed")) : true;
        return HttpResponse::json(m_recorder.start(name, onlyParsed, now()));
    }
    if (p == QLatin1String("/api/record/stop")) { KERS_METHODS("POST") return HttpResponse::json(m_recorder.stop()); }
    if (p == QLatin1String("/api/record/status")) { KERS_METHODS("GET") return HttpResponse::json(m_recorder.status(now())); }
#undef KERS_METHODS

    return HttpResponse::error(404, "The requested URL was not found on the server.");
}

}  // namespace kers::server
