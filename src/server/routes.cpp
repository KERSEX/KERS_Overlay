// Die Routen - Portierung der @app.route-Funktionen aus main.py.
//
// Antworten haben dieselbe JSON-Form wie in 0.2.6: regie.html, settings.html,
// das Web-Overlay und das Python-HUD (--url) haengen daran.
#include "kers_server.h"

#include "constants.h"
#include "designs.h"
#include "payload.h"
#include "pyconv.h"
#include "jsonfile.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
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
    // Browserquelle: die Farben des aktiven Designs (ab 0.3.1) haengen hinten an
    // core.css - so greifen sie in jeder Seite, ohne dass eine Vorlage sie kennt.
    if (clean == QLatin1String("css/core.css"))
        r.body += designCss();
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

HttpResponse KersServer::apiChampPoints(const HttpRequest& req)
{
    // POST {"file": "GFL_SL.json", "points_system": {"1": 20, ...}}  -> eigene Liste
    // POST {"file": "GFL_SL.json", "points_system": null}            -> zurueck auf Standard
    // Nur Dateien aus dem Datenordner (wie champ_file). Die eingebettete Vorlage
    // championship.json wird dabei als Kopie in den Datenordner geschrieben.
    const QJsonObject d = req.jsonObject();
    const QString name = py::basename(d.value("file"));
    if (name.isEmpty() || !m_settings.listChampFiles().contains(name))
        return HttpResponse::json({{"error", "unbekannte WM-Datei"}}, 400);
    const QString ziel = QDir(m_dataDir).filePath(name);
    bool ok = false;
    QJsonObject cfg = json::readObject(QFileInfo::exists(ziel) ? ziel : QStringLiteral(":/championship.json"), &ok);
    if (!ok)
        return HttpResponse::json({{"error", "WM-Datei nicht lesbar"}}, 400);

    const QJsonValue roh = d.value("points_system");
    QJsonObject neu;
    if (roh.isObject() || roh.isArray()) {
        const QMap<int, double> t = pointsSystem(QJsonObject{{"points_system", roh}});
        for (auto it = t.begin(); it != t.end(); ++it) {
            if (it.value() > 100 || it.value() != std::floor(it.value()))
                return HttpResponse::json({{"error", QStringLiteral("P%1: ganze Zahl von 0 bis 100").arg(it.key())}}, 400);
            neu[QString::number(it.key())] = static_cast<int>(it.value());
        }
    } else if (!roh.isNull() && !roh.isUndefined()) {
        return HttpResponse::json({{"error", "points_system muss eine Liste sein"}}, 400);
    }
    if (neu.isEmpty())
        cfg.remove("points_system");
    else
        cfg["points_system"] = neu;
    if (!json::writeObject(ziel, cfg))
        return HttpResponse::json({{"error", "WM-Datei nicht schreibbar"}}, 500);
    qInfo().noquote() << "[WM]" << name << (neu.isEmpty() ? "Punkte: Standard" : "Punkte: eigene Liste");
    emit settingsChanged();
    return HttpResponse::json({{"ok", true}, {"file", name}, {"championship", championship()}});
}

QByteArray KersServer::designCss() const
{
    const QString id = m_settings.settings().value("design").toString();
    const designs::Info d = designs::find(m_dataDir, id);
    if (d.id == QLatin1String(designs::DEFAULT_ID))
        return {};                                  // Standard: core.css bleibt, wie sie ist
    const QJsonObject t = designs::readTokens(d);
    const QJsonObject farben = t.value("colors").toObject();
    const QJsonObject alpha = t.value("alpha").toObject();
    QByteArray css = "\n/* Design \"" + d.name.toUtf8().replace("*/", "") + "\" - nur die Farben (ab 0.3.1) */\n:root {\n";
    auto voll = [&](const char* token, const char* var) {
        const QString v = farben.value(QLatin1String(token)).toString();
        if (QColor::isValidColorName(v))
            css += QByteArray("    ") + var + ": " + QColor(v).name().toLatin1() + ";\n";
    };
    auto flaeche = [&](const char* token, const char* var) {
        const QString v = farben.value(QLatin1String(token)).toString();
        if (!QColor::isValidColorName(v))
            return;
        const QColor c(v);
        const double a = alpha.value(QLatin1String(token)).toDouble(1.0);
        css += QByteArray("    ") + var + ": rgba(" + QByteArray::number(c.red()) + ", " + QByteArray::number(c.green()) + ", "
            + QByteArray::number(c.blue()) + ", calc(" + QByteArray::number(a, 'f', 3) + " * var(--ui-alpha)));\n";
    };
    voll("textMain", "--text-main");
    voll("textMuted", "--text-muted");
    flaeche("panel", "--bg-color");
    flaeche("panel", "--panel-bg");
    flaeche("row", "--row-bg");
    flaeche("rowAlt", "--row-bg-alt");
    flaeche("headBg1", "--head-bg-1");
    flaeche("headBg2", "--head-bg-2");
    flaeche("towerHead1", "--towerhead-1");
    flaeche("towerHead2", "--towerhead-2");
    const QJsonValue radius = t.value("shape").toObject().value("panelRadius");
    if (radius.isDouble())
        css += "    --panel-radius: " + QByteArray::number(std::clamp(radius.toInt(), 0, 40)) + "px;\n";
    return css + "}\n";
}

HttpResponse KersServer::apiDesigns(const HttpRequest& req, const AccessGuard::Result& zugang)
{
    const bool lokal = zugang.verdict == AccessGuard::Verdict::Local;
    const QString& p = req.path;
    if (p == QLatin1String("/api/designs")) {
        QJsonArray liste;
        for (const designs::Info& d : designs::list(m_dataDir))
            liste.append(QJsonObject{{"id", d.id}, {"name", d.name}, {"description", d.description},
                                     {"author", d.author}, {"builtin", d.builtin}, {"has_qml", d.hasQml}});
        const QString cur = m_settings.settings().value("design").toString();
        return HttpResponse::json({{"current", designs::find(m_dataDir, cur).id},
                                   {"designs", liste},
                                   {"dir", QDir::toNativeSeparators(designs::userRoot(m_dataDir))},
                                   {"local", lokal}});
    }
    if (!istPost(req))
        return HttpResponse::methodNotAllowed("POST, OPTIONS");
    // Kopieren und Ordner oeffnen schreiben bzw. oeffnen etwas auf DIESEM PC.
    if (!lokal)
        return HttpResponse::json({{"error", "Nur am PC mit KERS"}}, 403);
    if (p == QLatin1String("/api/designs/export")) {
        const QJsonObject d = req.jsonObject();
        QString fehler;
        const QString id = designs::exportDesign(m_dataDir, d.value("from").toString(), d.value("name").toString(), &fehler);
        if (id.isEmpty())
            return HttpResponse::json({{"ok", false}, {"error", fehler}}, 400);
        return HttpResponse::json({{"ok", true}, {"id", id},
                                   {"dir", QDir::toNativeSeparators(QDir(designs::userRoot(m_dataDir)).filePath(id))}});
    }
    if (p == QLatin1String("/api/designs/open")) {
        const QString ordner = designs::userRoot(m_dataDir);
        QDir().mkpath(ordner);
        QDesktopServices::openUrl(QUrl::fromLocalFile(ordner));
        return HttpResponse::json({{"ok", true}});
    }
    return HttpResponse::error(404);
}

HttpResponse KersServer::apiObs(const HttpRequest& req, const AccessGuard::Result& zugang)
{
    if (req.path == QLatin1String("/api/obs/config")) {
        if (!istPost(req))
            return HttpResponse::json(m_obs.configJson());
        // ⚠ Nur an DIESEM PC: wer Host/Port umbiegen kann, bekaeme OBS' Anmelde-Antwort
        // auf seinen eigenen Rechner (und koennte das Passwort dagegen raten).
        if (zugang.verdict != AccessGuard::Verdict::Local)
            return HttpResponse::json({{"error", "OBS-Verbindung nur an diesem PC einstellbar"}}, 403);
        const QJsonObject d = req.jsonObject();
        ObsLink::Config c = m_obs.config();
        if (d.contains("enabled"))
            c.enabled = py::truthy(d.value("enabled"));
        if (d.value("host").isString())
            c.host = d.value("host").toString().trimmed().left(253);
        if (const auto port = py::toInt(d.value("port")))
            c.port = static_cast<int>(*port);
        const bool pw = d.value("password").isString();
        if (pw)
            c.password = d.value("password").toString();
        m_obs.setConfig(c, pw);
        return HttpResponse::json(m_obs.configJson());
    }
    if (!istPost(req))
        return HttpResponse::json(m_obs.state());
    const QString fehler = m_obs.action(req.jsonObject());
    if (!fehler.isEmpty())
        return HttpResponse::json({{"ok", false}, {"error", fehler}}, 400);
    return HttpResponse::json({{"ok", true}});
}

// ── Zugriffsschutz ───────────────────────────────────────────────────────────
HttpResponse KersServer::handle(const HttpRequest& req)
{
    const AccessGuard::Result zugang = m_access.check(req);
    if (auto r = wache(req, zugang))
        return *r;
    HttpResponse res = route(req, zugang);
    // Mit ?key= gekommen (OBS-Quelle auf einem zweiten PC, Link aus dem
    // Schaltbrett): den Schluessel als Cookie mitgeben, damit die Seite ihre
    // eigenen Abrufe (/api/live, /api/stream) ohne ?key= machen kann.
    if (zugang.keyFromQuery)
        res.addHeader("Set-Cookie", AccessGuard::cookieHeader(zugang.token));
    return res;
}

std::optional<HttpResponse> KersServer::wache(const HttpRequest& req, const AccessGuard::Result& zugang) const
{
    using V = AccessGuard::Verdict;
    const QString& p = req.path;
    const bool api = p.startsWith(QLatin1String("/api/"));
    if (zugang.verdict == V::Blocked) {
        HttpResponse r = api ? HttpResponse::json({{"error", "gesperrt"}}, 403)
                             : HttpResponse::error(403, "Dieses Geraet ist in KERS Subsystems gesperrt.");
        r.closeConnection = true;
        return r;
    }

    // Fremde Webseiten: ein Formular oder fetch() von irgendeiner Seite im Browser
    // DIESES PCs kaeme sonst als "lokal" durch (z. B. /api/cam, /api/settings/reset).
    // Browser schicken bei POST immer Origin mit; passt der nicht zum Host, ist
    // es nicht unsere Seite. Dazu duerfen POSTs keinen der "einfachen" Typen
    // haben, die ein fremdes Formular ohne Rueckfrage des Browsers senden kann.
    if (req.method != "GET" && req.method != "OPTIONS") {
        const QByteArray origin = req.header("origin").trimmed().toLower();
        if (!origin.isEmpty()) {
            const int schema = origin.indexOf("://");
            const QByteArray originHost = schema > 0 ? origin.mid(schema + 3) : QByteArray();
            if (originHost.isEmpty() || originHost != req.header("host").trimmed().toLower())
                return HttpResponse::json({{"error", "fremde Herkunft"}}, 403);
        }
        const QByteArray ct = req.header("content-type").trimmed().toLower();
        if (ct.startsWith("text/plain") || ct.startsWith("application/x-www-form-urlencoded")
            || ct.startsWith("multipart/form-data"))
            return HttpResponse::json({{"error", "Content-Type muss application/json sein"}}, 415);
    }

    if (zugang.verdict == V::Local || zugang.verdict == V::Allowed) {
        // Spielwiesen nur auf diesem PC
        if (zugang.verdict != V::Local && (p == QLatin1String("/test") || p == QLatin1String("/opti")))
            return HttpResponse::error(404, "The requested URL was not found on the server.");
        return std::nullopt;
    }

    // Fremd ohne Schluessel: Login-Seite samt ihren Schriften, sonst nichts.
    if (p.startsWith(QLatin1String("/static/")) || p == QLatin1String("/api/login"))
        return std::nullopt;
    if (api)
        return HttpResponse::json({{"error", "login"}, {"login", "/login"}}, 401);
    if (req.method == "GET")
        return loginPage();
    return HttpResponse::error(401);
}

HttpResponse KersServer::loginPage() const
{
    bool ok = false;
    QByteArray html = lesen(QStringLiteral(":/templates/login.html"), &ok);
    if (!ok)
        html = "<!doctype html><title>KERS</title><p>Code aus KERS Subsystems eingeben: POST /api/login</p>";
    return HttpResponse::html(html, 401);
}

HttpResponse KersServer::apiLogin(const HttpRequest& req)
{
    const QJsonObject d = req.jsonObject();
    QByteArray token;
    QString id;
    int warten = 0, rest = 0;
    const QString code = d.value("code").toVariant().toString();
    const QString name = d.value("name").toString();
    switch (m_access.login(req.peer, code, name, QString::fromUtf8(req.header("user-agent")), &token, &id, &warten, &rest)) {
    case AccessGuard::Login::Ok: {
        HttpResponse r = HttpResponse::json({{"ok", true}, {"key", QString::fromLatin1(token)}});
        r.addHeader("Set-Cookie", AccessGuard::cookieHeader(token));
        return r;
    }
    case AccessGuard::Login::Wrong:
        return HttpResponse::json({{"ok", false}, {"error", "Code falsch"}, {"rest", rest}}, 403);
    case AccessGuard::Login::Locked:
        return HttpResponse::json({{"ok", false},
                                   {"error", QStringLiteral("Zu viele Versuche - bitte %1 s warten").arg(warten)},
                                   {"wait", warten}},
                                  429);
    case AccessGuard::Login::Blocked: {
        HttpResponse r = HttpResponse::json({{"ok", false}, {"error", "gesperrt"}}, 403);
        r.closeConnection = true;
        return r;
    }
    }
    return HttpResponse::error(500);
}

HttpResponse KersServer::route(const HttpRequest& req, const AccessGuard::Result& zugang)
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
    if (p == QLatin1String("/login")) { KERS_METHODS("GET") return loginPage(); }
    if (p == QLatin1String("/api/login")) { KERS_METHODS("POST") return apiLogin(req); }
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
        const QString geraet = zugang.deviceId;
        const bool lokal = zugang.verdict == AccessGuard::Verdict::Local;
        return HttpResponse::eventStream(
            [this, tick, slim, geraet, lokal](HttpConnection* c) { m_sse.add(c, tick, slim, geraet, lokal); });
    }
    if (p == QLatin1String("/api/status")) { KERS_METHODS("GET") return HttpResponse::json(status()); }
    if (p == QLatin1String("/api/track")) { KERS_METHODS("GET") return HttpResponse::json(trackJson()); }
    if (p == QLatin1String("/api/tracks")) { KERS_METHODS("GET", "POST") return apiTracks(req); }
    if (p == QLatin1String("/api/track/relearn")) {
        KERS_METHODS("POST")                     // bis 0.3.0 auch GET - ein <img src> reichte zum Ausloesen
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
    if (p == QLatin1String("/api/champfile/points")) { KERS_METHODS("POST") return apiChampPoints(req); }
    if (p == QLatin1String("/api/obs") || p == QLatin1String("/api/obs/config")) {
        KERS_METHODS("GET", "POST")
        return apiObs(req, zugang);
    }
    if (p == QLatin1String("/api/designs") || p.startsWith(QLatin1String("/api/designs/"))) {
        KERS_METHODS("GET", "POST")
        return apiDesigns(req, zugang);
    }
    if (p == QLatin1String("/api/obs/stream")) {
        KERS_METHODS("GET")
        const QByteArray erst = QJsonDocument(m_obs.state()).toJson(QJsonDocument::Compact);
        const QString geraet = zugang.deviceId;
        const bool lokal = zugang.verdict == AccessGuard::Verdict::Local;
        return HttpResponse::eventStream(
            [this, erst, geraet, lokal](HttpConnection* c) { m_obsChannel.add(c, erst, geraet, lokal); });
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
