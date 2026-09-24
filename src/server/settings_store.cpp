#include "settings_store.h"

#include "constants.h"
#include "jsonfile.h"
#include "pyconv.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace kers::server {

namespace {

const char* const kSettingsFile = "overlay_settings.json";
const char* const kPresetsFile = "presets.json";
const char* const kLayoutsFile = "layouts.json";
const char* const kChampDefault = "championship.json";
const QStringList kChampHidden = {"overlay_settings.json", "presets.json", "layouts.json", "tracks.json", "hud_state.json"};

const QStringList& camTasten()
{
    static QStringList t = []() {
        QStringList l;
        for (char c = 'a'; c <= 'z'; ++c)
            l << QString(QLatin1Char(c));
        for (char c = '0'; c <= '9'; ++c)
            l << QString(QLatin1Char(c));
        for (int i = 1; i <= 12; ++i)
            l << QStringLiteral("f%1").arg(i);
        l << "left" << "right" << "up" << "down" << "tab" << "space" << "enter"
          << "pageup" << "pagedown" << "home" << "end" << "insert" << "delete"
          << "add" << "subtract" << "multiply" << "divide" << "period";
        return l;
    }();
    return t;
}

// In main.py sind diese Vorgaben float (0.0, 1.0 ...), alle anderen Zahlen int.
// QJsonValue kennt den Unterschied nicht - deshalb die Liste.
const QStringList kFloatKeys = {"scale", "opacity", "text_outline", "dotsize", "ltdur", "flbdur", "battlethresh"};

}  // namespace

std::optional<QJsonObject> layoutPruefen(const QJsonValue& v)
{
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject roh = v.toObject();
    QJsonObject sauber;
    for (auto it = roh.begin(); it != roh.end(); ++it) {
        if (!isLayoutTeil(it.key()) || !it.value().isObject())
            continue;
        const QJsonObject w = it.value().toObject();
        QJsonObject e;
        if (w.contains("ecke")) {
            const QString ecke = py::toStr(w.value("ecke"));
            e["ecke"] = LAYOUT_ECKEN.contains(ecke) ? ecke : QStringLiteral("tl");
        }
        // ⚠ Wie in main.py: scheitert EIN Feld (int("1.5"), null ...), faellt der
        // ganze Eintrag weg - nicht nur das Feld.
        bool kaputt = false;
        auto zahl = [&](const char* k, long long lo, long long hi) {
            if (kaputt || !w.contains(QLatin1String(k)))
                return;
            const auto n = py::toInt(w.value(QLatin1String(k)));
            if (!n) {
                kaputt = true;
                return;
            }
            e[QLatin1String(k)] = static_cast<int>(std::clamp(*n, lo, hi));
        };
        // 4000 statt "Fensterbreite": reicht fuer 4K und haelt Unsinn wie 1e9 draussen.
        zahl("dx", -4000, 4000);
        zahl("dy", -4000, 4000);
        zahl("z", 0, 100);
        if (!kaputt && w.contains("groesse")) {
            // Grenzen wie beim Regler; auf zwei Stellen gerundet.
            const auto g = py::toFloat(w.value("groesse"));
            if (g && !std::isnan(*g))
                e["groesse"] = py::round(std::clamp(*g, 0.5, 2.0), 2);
            else
                kaputt = true;
        }
        if (kaputt)
            continue;
        // ⚠ Ein Eintrag ohne ein einziges gueltiges Feld sagt nichts aus. Weg damit.
        if (!e.isEmpty())
            sauber[it.key()] = e;
    }
    return sauber;
}

QString camTasteOk(const QString& wert)
{
    QStringList teile;
    for (const QString& t : wert.split(QLatin1Char('+'))) {
        const QString s = t.trimmed().toLower();
        if (!s.isEmpty())
            teile << s;
    }
    if (teile.isEmpty() || teile.size() > 3)
        return QString();
    const QString taste = teile.last();
    if (!camTasten().contains(taste))
        return QString();
    for (int i = 0; i < teile.size() - 1; ++i)
        if (teile.at(i) != QLatin1String("shift") && teile.at(i) != QLatin1String("ctrl") && teile.at(i) != QLatin1String("alt"))
            return QString();
    return teile.join(QLatin1Char('+'));
}

const QJsonObject& SettingsStore::defaults()
{
    static const QJsonObject d = {
        // Sichtbarkeit der Komponenten
        {"tower", true}, {"battles", true}, {"map", true}, {"onboard", true},
        {"lights", true}, {"damage", true}, {"msgs", true}, {"ticker", true},
        {"lowerthird", true}, {"flbanner", true},
        {"undercut", true}, {"deltabar", true}, {"ampel", true}, {"mapnumbers", true}, {"mapflags", true},
        {"pred", true}, {"danger", true}, {"comeback", true}, {"pbflash", true}, {"fresh", true},
        {"strat", true}, {"pitproj", true},
        {"brand_title", ""}, {"brand_accent", "#e10600"},
        {"tower_logo", ""}, {"tower_logo_pos", "left"}, {"tower_logo_h", 34},
        {"header_color", ""}, {"row_color", ""},
        // ⚠ scale/opacity/text_outline MUESSEN Gleitkomma bleiben (Typcast beim POST).
        {"scale", 0.0}, {"opacity", 1.0}, {"text_outline", 0.0},
        {"rows", 0}, {"mapsize", 400}, {"maprot", 110}, {"mapflip", true}, {"mapcorner", "tr"},
        {"dotsize", 1.0}, {"holds", 300}, {"ltdur", 4.0}, {"flbdur", 4.5}, {"dmgcrit", 60},
        {"battlethresh", 1.5}, {"battleboxes", 4}, {"hotlapboxes", 4},
        {"battledir", "row"}, {"penside", "left"}, {"penhidefinish", true}, {"podiumstil", "flat"},
        {"camkeys", true}, {"camnextkey", "f8"}, {"camholdms", 60},
        {"trackname", true},
        {"layout", QJsonObject()},
        {"champ_file", ""}, {"preset", "voll"},
    };
    return d;
}

QJsonObject SettingsStore::layoutStandard()
{
    auto e = [](const char* ecke, int dx, int dy, int z) {
        return QJsonObject{{"ecke", ecke}, {"dx", dx}, {"dy", dy}, {"z", z}, {"groesse", 1.0}};
    };
    return QJsonObject{
        {"tower", e("tl", 48, 10, 30)}, {"trackmap", e("tr", 12, 12, 42)}, {"racemsg", e("tc", 0, 22, 46)},
        {"flbanner", e("tc", 0, 84, 47)}, {"lights", e("tc", 0, 170, 80)}, {"battles", e("bc", 0, 28, 40)},
        {"hotlaps", e("bc", 0, 28, 40)}, {"lowerthird", e("bc", 0, 380, 41)}, {"danger", e("bc", 0, 40, 46)},
        {"onboard", e("bl", 24, 28, 44)}, {"pitproj", e("bl", 24, 24, 44)}, {"pitcards", e("br", 24, 28, 45)},
        {"champ", e("br", 24, 40, 45)},
    };
}

SettingsStore::SettingsStore(const QString& dataDir)
    : m_dataDir(dataDir)
{
    load();
}

void SettingsStore::load()
{
    const QJsonObject& def = defaults();
    m_settings = def;
    bool ok = false;
    const QJsonObject saved = json::readObject(QDir(m_dataDir).filePath(QLatin1String(kSettingsFile)), &ok);
    if (ok)
        for (auto it = saved.begin(); it != saved.end(); ++it)
            if (def.contains(it.key()))
                m_settings[it.key()] = it.value();
    // Eine frueher gespeicherte, inzwischen abgeschaffte Trackmap-Position ("tl"/"lc").
    if (!MAP_CORNERS.contains(m_settings.value("mapcorner").toString()))
        m_settings["mapcorner"] = def.value("mapcorner");

    // Presets: unbekannte Keys verwerfen, "preset" nie mitnehmen
    m_presets = QJsonObject();
    const QJsonObject rawP = json::readObject(QDir(m_dataDir).filePath(QLatin1String(kPresetsFile)));
    // Wie list(raw.items())[:PRESETS_MAX]: die ersten 12 Eintraege, auch ungueltige zaehlen mit
    int n = 0;
    for (auto it = rawP.begin(); it != rawP.end() && n < PRESETS_MAX; ++it, ++n) {
        if (!it.value().isObject())
            continue;
        QJsonObject vals;
        const QJsonObject v = it.value().toObject();
        for (auto k = v.begin(); k != v.end(); ++k)
            if (def.contains(k.key()) && k.key() != QLatin1String("preset"))
                vals[k.key()] = k.value();
        m_presets[it.key().left(PRESET_NAME_MAX)] = vals;
    }

    // Layouts: dieselbe Pruefung wie ein Layout aus dem Request
    m_layouts = QJsonObject();
    const QJsonObject rawL = json::readObject(QDir(m_dataDir).filePath(QLatin1String(kLayoutsFile)));
    n = 0;
    for (auto it = rawL.begin(); it != rawL.end() && n < LAYOUTS_MAX; ++it, ++n) {
        const auto sauber = layoutPruefen(it.value());
        if (!sauber)
            continue;
        m_layouts[it.key().left(LAYOUT_NAME_MAX)] = *sauber;
    }
}

void SettingsStore::save()
{
    if (!json::writeObject(QDir(m_dataDir).filePath(QLatin1String(kSettingsFile)), m_settings))
        qWarning() << "[SETTINGS] Speichern fehlgeschlagen";
}

void SettingsStore::savePresets()
{
    if (!json::writeObject(QDir(m_dataDir).filePath(QLatin1String(kPresetsFile)), m_presets))
        qWarning() << "[PRESETS] Speichern fehlgeschlagen";
}

void SettingsStore::saveLayouts()
{
    if (!json::writeObject(QDir(m_dataDir).filePath(QLatin1String(kLayoutsFile)), m_layouts))
        qWarning() << "[LAYOUTS] Speichern fehlgeschlagen";
}

void SettingsStore::set(const QString& key, const QJsonValue& value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings[key] = value;
    save();
}

void SettingsStore::reset()
{
    m_settings = defaults();
    save();
}

bool SettingsStore::apply(const QJsonObject& patch)
{
    const QJsonObject& def = defaults();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const QString k = it.key();
        const QJsonValue v = it.value();
        if (!def.contains(k))
            continue;
        const QString sv = py::toStr(v);
        // Logo-Name: nur ein Dateiname aus static/logos/, kein Pfad ("../").
        if (k == QLatin1String("tower_logo")) {
            const QString name = py::basename(v);
            m_settings[k] = (!name.isEmpty() && listLogos().contains(name)) ? name : QString();
            continue;
        }
        if (k == QLatin1String("tower_logo_pos")) {
            m_settings[k] = sv == QLatin1String("right") ? QStringLiteral("right") : QStringLiteral("left");
            continue;
        }
        if (k == QLatin1String("mapcorner")) {
            m_settings[k] = MAP_CORNERS.contains(sv) ? sv : def.value(k).toString();
            continue;
        }
        // WM-Stand-Datei: nur ein Dateiname aus dem Datenordner, kein Pfad.
        if (k == QLatin1String("champ_file")) {
            const QString name = py::basename(v);
            m_settings[k] = (!name.isEmpty() && listChampFiles().contains(name)) ? name : QString();
            continue;
        }
        if (k == QLatin1String("layout")) {
            const auto sauber = layoutPruefen(v);
            if (sauber)
                m_settings[k] = *sauber;
            continue;
        }
        if (k == QLatin1String("battledir")) {
            m_settings[k] = sv == QLatin1String("column") ? QStringLiteral("column") : QStringLiteral("row");
            continue;
        }
        if (k == QLatin1String("penside")) {
            m_settings[k] = sv == QLatin1String("right") ? QStringLiteral("right") : QStringLiteral("left");
            continue;
        }
        if (k == QLatin1String("podiumstil")) {
            m_settings[k] = sv == QLatin1String("metal") ? QStringLiteral("metal") : QStringLiteral("flat");
            continue;
        }
        // ⚠ Nur eine Taste aus der erlaubten Liste - wird auf dem Spiel-PC gedrueckt.
        if (k == QLatin1String("camnextkey")) {
            m_settings[k] = camTasteOk(py::truthy(v) ? sv : QString());
            continue;
        }
        if (k == QLatin1String("camholdms")) {
            if (const auto n = py::toInt(v))
                m_settings[k] = static_cast<int>(std::clamp(*n, 10LL, 300LL));
            continue;
        }
        if (k == QLatin1String("text_outline")) {
            const auto d = py::toFloat(v);
            if (d && std::isfinite(*d))
                m_settings[k] = py::round(std::clamp(*d, 0.0, 1.0), 2);
            continue;
        }
        if (k == QLatin1String("battleboxes") || k == QLatin1String("hotlapboxes")) {
            if (const auto n = py::toInt(v))
                m_settings[k] = static_cast<int>(std::clamp(*n, 0LL, 4LL));
            continue;
        }
        // Allgemeiner Zweig wie in main.py: der Typ der Vorgabe entscheidet.
        //   bool  -> bool(v)
        //   Zahl  -> int(v) / float(v); ein bool im Request setzt die VORGABE
        //   Text  -> str(v)
        const QJsonValue dv = def.value(k);
        if (dv.isBool()) {
            m_settings[k] = py::truthy(v);
        } else if (dv.isDouble()) {
            if (v.isBool()) {
                m_settings[k] = dv;
            } else if (kFloatKeys.contains(k)) {
                const auto d = py::toFloat(v);
                if (d && std::isfinite(*d))
                    m_settings[k] = *d;
            } else if (const auto n = py::toInt(v)) {
                m_settings[k] = static_cast<qint64>(*n);
            }
        } else if (dv.isString()) {
            m_settings[k] = sv;
        }
    }
    save();
    return patch.contains("maprot") || patch.contains("mapflip");
}

QString SettingsStore::presetSichern(const QString& nameIn)
{
    const QString name = nameIn.trimmed().left(PRESET_NAME_MAX);
    if (name.isEmpty())
        return QStringLiteral("name fehlt");
    if (!m_presets.contains(name) && m_presets.size() >= PRESETS_MAX)
        return QStringLiteral("maximal %1 Presets").arg(PRESETS_MAX);
    // Der Schnappschuss kommt bewusst vom SERVER, nicht aus dem Request.
    QJsonObject snap = m_settings;
    snap.remove("preset");
    m_presets[name] = snap;
    savePresets();
    return QString();
}

void SettingsStore::presetLoeschen(const QString& name)
{
    m_presets.remove(name.trimmed().left(PRESET_NAME_MAX));
    savePresets();
}

QString SettingsStore::layoutSichern(const QString& nameIn)
{
    const QString name = nameIn.trimmed().left(LAYOUT_NAME_MAX);
    if (name.isEmpty())
        return QStringLiteral("name fehlt");
    if (!m_layouts.contains(name) && m_layouts.size() >= LAYOUTS_MAX)
        return QStringLiteral("maximal %1 Layouts").arg(LAYOUTS_MAX);
    // Ein LEERES Layout ist ausdruecklich erlaubt ("alles auf Standard").
    m_layouts[name] = m_settings.value("layout").toObject();
    saveLayouts();
    return QString();
}

void SettingsStore::layoutLoeschen(const QString& name)
{
    m_layouts.remove(name.trimmed().left(LAYOUT_NAME_MAX));
    saveLayouts();
}

QStringList SettingsStore::listLogos()
{
    static const QStringList exts = {"png", "jpg", "jpeg", "webp", "gif", "svg"};
    QStringList out;
    const QDir dir(QStringLiteral(":/static/logos"));
    for (const QString& f : dir.entryList(QDir::Files, QDir::Name))
        if (exts.contains(QFileInfo(f).suffix().toLower()))
            out << f;
    return out;
}

QStringList SettingsStore::listChampFiles() const
{
    // championship.json steht immer an erster Stelle - auch wenn sie (noch) nicht
    // daneben liegt, denn dann greift die eingebettete Vorlage.
    QStringList found;
    const QDir dir(m_dataDir);
    for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        if (!kChampHidden.contains(f) && f != QLatin1String(kChampDefault))
            found << f;
    return QStringList{QLatin1String(kChampDefault)} + found;
}

bool SettingsStore::defaultChampExists() const
{
    return QFileInfo::exists(QDir(m_dataDir).filePath(QLatin1String(kChampDefault)));
}

QString SettingsStore::champPath() const
{
    const QString name = QFileInfo(m_settings.value("champ_file").toString()).fileName();
    if (!name.isEmpty()) {
        const QString chosen = QDir(m_dataDir).filePath(name);
        if (QFileInfo::exists(chosen))
            return chosen;
    }
    const QString def = QDir(m_dataDir).filePath(QLatin1String(kChampDefault));
    if (QFileInfo::exists(def))
        return def;
    return QStringLiteral(":/championship.json");     // eingebettete Vorlage
}

}  // namespace kers::server
