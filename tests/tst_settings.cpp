// Settings-Pruefung wie /api/settings in main.py: Whitelist, Typumwandlung nach
// Python-Regeln, Pfad-Schutz, Grenzen, Layout feldweise, Presets/Layouts <= 12.
#include "settings_store.h"

#include "constants.h"
#include "jsonfile.h"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace kers::server;

class TstSettings : public QObject {
    Q_OBJECT
private:
    static QJsonValue nach(const QJsonObject& patch, const char* key, SettingsStore* s = nullptr)
    {
        QTemporaryDir dir;
        SettingsStore eigen(dir.path());
        SettingsStore& st = s ? *s : eigen;
        st.apply(patch);
        return st.settings().value(QLatin1String(key));
    }

private slots:
    void whitelistUndBool()
    {
        QTemporaryDir dir;
        SettingsStore s(dir.path());
        s.apply({{"gibtsnicht", 1}, {"tower", false}});
        QVERIFY(!s.settings().contains("gibtsnicht"));
        QCOMPARE(s.settings().value("tower"), QJsonValue(false));
        // bool(v) nach Python: jeder nicht-leere Text ist wahr, auch "false"
        QCOMPARE(nach({{"tower", "false"}}, "tower"), QJsonValue(true));
        QCOMPARE(nach({{"tower", ""}}, "tower"), QJsonValue(false));
        QCOMPARE(nach({{"tower", 0}}, "tower"), QJsonValue(false));
        QCOMPARE(nach({{"tower", QJsonValue::Null}}, "tower"), QJsonValue(false));
    }

    void zahlen()
    {
        QCOMPARE(nach({{"rows", "12"}}, "rows"), QJsonValue(12));
        QCOMPARE(nach({{"rows", "1.5"}}, "rows"), QJsonValue(0));            // int("1.5") scheitert -> alt
        QCOMPARE(nach({{"rows", 7.9}}, "rows"), QJsonValue(7));
        QCOMPARE(nach({{"rows", -7.9}}, "rows"), QJsonValue(-7));
        QCOMPARE(nach({{"maprot", true}}, "maprot"), QJsonValue(110));       // bool -> Vorgabe
        QCOMPARE(nach({{"scale", "0.8"}}, "scale"), QJsonValue(0.8));        // float bleibt float
        QCOMPARE(nach({{"scale", 1}}, "scale"), QJsonValue(1.0));
        QCOMPARE(nach({{"battlethresh", "abc"}}, "battlethresh"), QJsonValue(1.5));
        QCOMPARE(nach({{"camholdms", 5}}, "camholdms"), QJsonValue(10));
        QCOMPARE(nach({{"camholdms", 1000}}, "camholdms"), QJsonValue(300));
        QCOMPARE(nach({{"battleboxes", 9}}, "battleboxes"), QJsonValue(4));
        QCOMPARE(nach({{"hotlapboxes", -2}}, "hotlapboxes"), QJsonValue(0));
        QCOMPARE(nach({{"text_outline", 0.456}}, "text_outline"), QJsonValue(0.46));
        QCOMPARE(nach({{"text_outline", 3}}, "text_outline"), QJsonValue(1.0));
        QCOMPARE(nach({{"text_outline", true}}, "text_outline"), QJsonValue(1.0));
    }

    void texteUndAuswahl()
    {
        QCOMPARE(nach({{"brand_title", 12}}, "brand_title"), QJsonValue("12"));
        QCOMPARE(nach({{"brand_title", true}}, "brand_title"), QJsonValue("True"));
        QCOMPARE(nach({{"mapcorner", "tl"}}, "mapcorner"), QJsonValue("tr"));     // Tower-Ecke gesperrt
        QCOMPARE(nach({{"mapcorner", "bl"}}, "mapcorner"), QJsonValue("bl"));
        QCOMPARE(nach({{"battledir", "column"}}, "battledir"), QJsonValue("column"));
        QCOMPARE(nach({{"battledir", "x"}}, "battledir"), QJsonValue("row"));
        QCOMPARE(nach({{"penside", "right"}}, "penside"), QJsonValue("right"));
        QCOMPARE(nach({{"podiumstil", "gold"}}, "podiumstil"), QJsonValue("flat"));
        QCOMPARE(nach({{"tower_logo_pos", "right"}}, "tower_logo_pos"), QJsonValue("right"));
    }

    void pfadeKommenNichtDurch()
    {
        QCOMPARE(nach({{"tower_logo", "../../evil.png"}}, "tower_logo"), QJsonValue(""));
        QTemporaryDir dir;
        SettingsStore s(dir.path());
        QCOMPARE(nach({{"champ_file", "..\\..\\Windows\\win.ini"}}, "champ_file", &s), QJsonValue(""));
        QFile f(dir.filePath("liga.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QCOMPARE(nach({{"champ_file", "presets.json"}}, "champ_file", &s), QJsonValue(""));     // eigene Datei
        // Nur der Dateiname zaehlt, der Pfad davor faellt weg
        QCOMPARE(nach({{"champ_file", "C:\\irgendwo\\liga.json"}}, "champ_file", &s), QJsonValue("liga.json"));
        QCOMPARE(s.champPath(), dir.filePath("liga.json"));
        s.apply({{"champ_file", ""}});
        QCOMPARE(s.champPath(), QStringLiteral(":/championship.json"));
    }

    void kameraTaste()
    {
        QCOMPARE(camTasteOk("Shift+F9"), QStringLiteral("shift+f9"));
        QCOMPARE(camTasteOk(" ctrl + alt + x "), QStringLiteral("ctrl+alt+x"));
        QCOMPARE(camTasteOk("win+r"), QString());
        QCOMPARE(camTasteOk("ctrl+alt+shift+x"), QString());          // mehr als drei Teile
        QCOMPARE(camTasteOk("f13"), QString());
        QCOMPARE(camTasteOk(""), QString());
        QCOMPARE(nach({{"camnextkey", "F8"}}, "camnextkey"), QJsonValue("f8"));
        QCOMPARE(nach({{"camnextkey", QJsonValue::Null}}, "camnextkey"), QJsonValue(""));
    }

    void layoutFeldweise()
    {
        auto pruef = [](const QJsonObject& o) { return *layoutPruefen(o); };
        QVERIFY(!layoutPruefen(QJsonArray{1}).has_value());
        QVERIFY(!layoutPruefen("x").has_value());
        QCOMPARE(pruef({{"tower", QJsonObject{{"z", 55}}}}), (QJsonObject{{"tower", QJsonObject{{"z", 55}}}}));
        QCOMPARE(pruef({{"tower", QJsonObject{{"ecke", "xx"}, {"dx", 99999}, {"dy", "-12"}}}}),
                 (QJsonObject{{"tower", QJsonObject{{"ecke", "tl"}, {"dx", 4000}, {"dy", -12}}}}));
        // Ein kaputtes Feld verwirft den ganzen Eintrag
        QCOMPARE(pruef({{"tower", QJsonObject{{"z", 5}, {"dx", "1.5"}}}}), QJsonObject());
        QCOMPARE(pruef({{"tower", QJsonObject{{"z", 5}, {"dy", QJsonValue::Null}}}}), QJsonObject());
        QCOMPARE(pruef({{"nope", QJsonObject{{"z", 1}}}}), QJsonObject());
        QCOMPARE(pruef({{"tower", QJsonObject()}}), QJsonObject());
        QCOMPARE(pruef({{"tower", QJsonObject{{"groesse", 3}}}}), (QJsonObject{{"tower", QJsonObject{{"groesse", 2.0}}}}));
        QCOMPARE(pruef({{"tower", QJsonObject{{"groesse", 1.3417}}}}), (QJsonObject{{"tower", QJsonObject{{"groesse", 1.34}}}}));
        QCOMPARE(pruef({{"champ", QJsonObject{{"z", 500}}}}), (QJsonObject{{"champ", QJsonObject{{"z", 100}}}}));

        QTemporaryDir dir;
        SettingsStore s(dir.path());
        s.apply({{"layout", QJsonObject{{"tower", QJsonObject{{"z", 55}}}}}});
        s.apply({{"layout", "kaputt"}});                             // kein Layout -> alter Stand bleibt
        QCOMPARE(s.settings().value("layout").toObject().value("tower").toObject().value("z").toInt(), 55);
    }

    void speichernUndLaden()
    {
        QTemporaryDir dir;
        {
            SettingsStore s(dir.path());
            QVERIFY(s.apply({{"maprot", 45}}));                      // Ausrichtung geaendert -> true
            QVERIFY(!s.apply({{"rows", 10}}));
        }
        SettingsStore s2(dir.path());
        QCOMPARE(s2.settings().value("maprot"), QJsonValue(45));
        QCOMPARE(s2.settings().value("rows"), QJsonValue(10));

        // Alte, abgeschaffte Trackmap-Ecke wird beim Laden zurueckgeholt; Fremdschluessel fallen weg
        kers::json::writeObject(dir.filePath("overlay_settings.json"), {{"mapcorner", "lc"}, {"alt", 1}, {"rows", 3}});
        SettingsStore s3(dir.path());
        QCOMPARE(s3.settings().value("mapcorner"), QJsonValue("tr"));
        QVERIFY(!s3.settings().contains("alt"));
        QCOMPARE(s3.settings().value("rows"), QJsonValue(3));
        QCOMPARE(s3.settings().value("tower"), QJsonValue(true));   // fehlende aus den Vorgaben
        s3.reset();
        QCOMPARE(s3.settings(), SettingsStore::defaults());
    }

    void presets()
    {
        QTemporaryDir dir;
        SettingsStore s(dir.path());
        s.apply({{"rows", 8}});
        QCOMPARE(s.presetSichern("   "), QStringLiteral("name fehlt"));
        for (int i = 0; i < PRESETS_MAX; ++i)
            QVERIFY(s.presetSichern(QStringLiteral("P%1").arg(i)).isEmpty());
        QCOMPARE(s.presetSichern("einer zu viel"), QStringLiteral("maximal 12 Presets"));
        QVERIFY(s.presetSichern("P3").isEmpty());                   // ueberschreiben geht immer
        QCOMPARE(s.presets().value("P3").toObject().value("rows").toInt(), 8);
        QVERIFY(!s.presets().value("P3").toObject().contains("preset"));
        s.presetLoeschen("P3");
        QCOMPARE(s.presets().size(), PRESETS_MAX - 1);
        QVERIFY(s.presetSichern(QString(40, QLatin1Char('x'))).isEmpty());
        QVERIFY(s.presets().contains(QString(30, QLatin1Char('x'))));

        // Laden: unbekannte Schluessel und "preset" fallen weg
        kers::json::writeObject(dir.filePath("presets.json"),
                                {{"A", QJsonObject{{"rows", 2}, {"preset", "x"}, {"fremd", 1}}}, {"B", 5}});
        SettingsStore s2(dir.path());
        QCOMPARE(s2.presets().size(), 1);
        QCOMPARE(s2.presets().value("A").toObject(), (QJsonObject{{"rows", 2}}));
    }

    void layouts()
    {
        QTemporaryDir dir;
        SettingsStore s(dir.path());
        QVERIFY(s.layoutSichern("Leer").isEmpty());                 // leeres Layout ist erlaubt
        QCOMPARE(s.layouts().value("Leer").toObject(), QJsonObject());
        s.apply({{"layout", QJsonObject{{"tower", QJsonObject{{"dx", 5}}}}}});
        QVERIFY(s.layoutSichern("Rennen").isEmpty());
        QCOMPARE(s.layouts().value("Rennen").toObject().value("tower").toObject().value("dx").toInt(), 5);
        for (int i = 0; i < LAYOUTS_MAX; ++i)
            s.layoutSichern(QStringLiteral("L%1").arg(i));
        QCOMPARE(s.layouts().size(), LAYOUTS_MAX);
        QCOMPARE(s.layoutSichern("neu"), QStringLiteral("maximal 12 Layouts"));
        s.layoutLoeschen("Rennen");
        QVERIFY(!s.layouts().contains("Rennen"));

        kers::json::writeObject(dir.filePath("layouts.json"),
                                {{"A", QJsonObject{{"tower", QJsonObject{{"z", 500}}}, {"nope", QJsonObject{{"z", 1}}}}},
                                 {"B", "kaputt"}});
        SettingsStore s2(dir.path());
        QCOMPARE(s2.layouts().size(), 1);
        QCOMPARE(s2.layouts().value("A").toObject(), (QJsonObject{{"tower", QJsonObject{{"z", 100}}}}));
    }

    void champDateien()
    {
        QTemporaryDir dir;
        SettingsStore s(dir.path());
        QCOMPARE(s.listChampFiles(), QStringList{"championship.json"});
        QVERIFY(!s.defaultChampExists());
        for (const char* n : {"b.json", "a.json", "tracks.json", "hud_state.json", "championship.json", "x.txt"}) {
            QFile f(dir.filePath(QLatin1String(n)));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QCOMPARE(s.listChampFiles(), (QStringList{"championship.json", "a.json", "b.json"}));
        QVERIFY(s.defaultChampExists());
        QCOMPARE(s.champPath(), dir.filePath("championship.json"));
    }

    void layoutStandardHatAlleBausteine()
    {
        const QJsonObject st = SettingsStore::layoutStandard();
        QCOMPARE(st.size(), 13);
        for (const LayoutTeil& t : LAYOUT_TEILE)
            QVERIFY(st.contains(QLatin1String(t.key)));
    }
};

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
