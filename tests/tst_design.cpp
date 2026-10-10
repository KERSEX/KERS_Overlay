// Designs (ab 0.3.1): Ordner finden, Namen pruefen, Szene zusammensetzen,
// kopieren - und das Theme liest design.json mit Rueckfall auf den Standard.
//
// Im Test gibt es keine eingebauten Designs (die stecken in der EXE) - geprueft
// wird mit eigenen Ordnern im Temp-Verzeichnis.
#include "designs.h"
#include "jsonfile.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>

using namespace kers;

namespace {

void schreibe(const QString& pfad, const QByteArray& inhalt)
{
    QDir().mkpath(QFileInfo(pfad).absolutePath());
    QFile f(pfad);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("nicht schreibbar");
    f.write(inhalt);
}

}  // namespace

class TstDesign : public QObject {
    Q_OBJECT
private slots:
    void namen()
    {
        QVERIFY(designs::validName("Mein F1 TV"));
        QVERIFY(designs::validName("liga_2026-v2"));
        QVERIFY(designs::validName("Überholt"));
        QVERIFY(!designs::validName(""));
        QVERIFY(!designs::validName(".laufzeit"));
        QVERIFY(!designs::validName("../boese"));
        QVERIFY(!designs::validName("a/b"));
        QVERIFY(!designs::validName("con\\x"));
        QVERIFY(!designs::validName(QString(41, QLatin1Char('a'))));
        QVERIFY(!designs::validName("ende."));
    }

    void findenUndListe()
    {
        QTemporaryDir dir;
        const QString wurzel = designs::userRoot(dir.path());
        schreibe(wurzel + "/Liga/design.json", "{\"name\":\"Liga-Look\",\"author\":\"KERS\",\"description\":\"Test\"}");
        schreibe(wurzel + "/OhneJson/qml/Overlay.qml", "import QtQuick\nItem {}\n");   // kein design.json -> zaehlt nicht
        schreibe(wurzel + "/.laufzeit/x/design.json", "{}");                           // versteckt
        const QList<designs::Info> l = designs::list(dir.path());
        QCOMPARE(l.size(), 1);
        QCOMPARE(l.first().name, QStringLiteral("Liga-Look"));
        QVERIFY(!l.first().builtin);
        QVERIFY(!l.first().hasQml);
        QVERIFY(designs::exists(dir.path(), "liga"));                 // gross/klein egal
        QVERIFY(!designs::exists(dir.path(), "OhneJson"));
        // Unbekannt -> Standard (im Test ein leeres Platzhalter-Design)
        QCOMPARE(designs::find(dir.path(), "gibtsnicht").id, QStringLiteral("kers"));
        QCOMPARE(designs::find(dir.path(), "Liga").name, QStringLiteral("Liga-Look"));
    }

    void szeneZusammensetzen()
    {
        QTemporaryDir dir;
        const QString wurzel = designs::userRoot(dir.path());
        schreibe(wurzel + "/Voll/design.json", "{\"name\":\"Voll\"}");
        schreibe(wurzel + "/Voll/qml/Overlay.qml", "import QtQuick\nItem {}\n");
        schreibe(wurzel + "/Voll/qml/parts/Tower.qml", "import QtQuick\nItem {}\n");
        schreibe(wurzel + "/Voll/qml/notiz.txt", "keine Szenen-Datei");
        const designs::Info d = designs::find(dir.path(), "Voll");
        QVERIFY(d.hasQml);
        QString fehler;
        const QString lauf = designs::prepareScene(dir.path(), d, &fehler, "obs");
        QVERIFY2(!lauf.isEmpty(), qPrintable(fehler));
        QVERIFY(lauf.endsWith("Voll-obs"));
        QVERIFY(QFile::exists(lauf + "/Overlay.qml"));
        QVERIFY(QFile::exists(lauf + "/parts/Tower.qml"));
        QVERIFY(!QFile::exists(lauf + "/notiz.txt"));                // nur .qml/.js

        // Ohne Overlay.qml (und ohne eingebaute Szene im Test) -> Fehler, Rueckfall
        schreibe(wurzel + "/Teil/design.json", "{}");
        schreibe(wurzel + "/Teil/qml/parts/Tower.qml", "import QtQuick\nItem {}\n");
        QVERIFY(designs::prepareScene(dir.path(), designs::find(dir.path(), "Teil"), &fehler).isEmpty());
        QVERIFY(fehler.contains("Overlay.qml"));
        // Ohne qml-Ordner -> eingebaute Szene (leer, kein Fehler)
        schreibe(wurzel + "/Farben/design.json", "{}");
        QVERIFY(designs::prepareScene(dir.path(), designs::find(dir.path(), "Farben")).isEmpty());
    }

    void kopieren()
    {
        QTemporaryDir dir;
        const QString wurzel = designs::userRoot(dir.path());
        schreibe(wurzel + "/Basis/design.json", "{\"name\":\"Basis\",\"colors\":{\"accent\":\"#00ff00\"}}");
        schreibe(wurzel + "/Basis/qml/parts/Tower.qml", "import QtQuick\nItem {}\n");
        schreibe(wurzel + "/Basis/fonts/x.ttf", "kein echter Font");
        QString fehler;
        QCOMPARE(designs::exportDesign(dir.path(), "Basis", "Meins", &fehler), QStringLiteral("Meins"));
        const QJsonObject j = json::readObject(wurzel + "/Meins/design.json");
        QCOMPARE(j.value("name").toString(), QStringLiteral("Meins"));
        QCOMPARE(j.value("based_on").toString(), QStringLiteral("Basis"));
        QCOMPARE(j.value("colors").toObject().value("accent").toString(), QStringLiteral("#00ff00"));
        QVERIFY(QFile::exists(wurzel + "/Meins/qml/parts/Tower.qml"));
        QVERIFY(QFile::exists(wurzel + "/Meins/fonts/x.ttf"));
        // Gibt es schon / ungueltiger Name
        QVERIFY(designs::exportDesign(dir.path(), "Basis", "Meins", &fehler).isEmpty());
        QVERIFY(fehler.contains("schon"));
        QVERIFY(designs::exportDesign(dir.path(), "Basis", "../raus", &fehler).isEmpty());
        QVERIFY(!QDir(dir.filePath("raus")).exists());
    }

    void themeLiestTokens()
    {
        hud::Theme t;
        const QColor panelVorher = t.panelBg();
        QCOMPARE(t.panelRadius(), 10);
        QCOMPARE(t.sans(), QStringLiteral("Inter"));

        const QJsonObject tokens{
            {"colors", QJsonObject{{"accent", "#00aaff"}, {"panel", "#102030"}, {"textMuted", "kaputt"}}},
            {"alpha", QJsonObject{{"panel", 0.5}, {"row", 7}}},              // 7 -> auf 1 begrenzt
            {"shape", QJsonObject{{"panelRadius", 4}, {"rowHeight", 5}}},   // 5 -> mindestens 24
            {"fonts", QJsonObject{{"sans", QJsonArray{"Gibt Es Nicht Sans"}}}},
            {"extra", QJsonObject{{"posBg", "#14151a"}}}};
        t.applyDesign("test", tokens, QString());
        QCOMPARE(t.design(), QStringLiteral("test"));
        QCOMPARE(t.accent(), QColor("#00aaff"));
        QCOMPARE(t.panelBg().red(), 0x10);
        QVERIFY(qAbs(t.panelBg().alphaF() - 0.5) < 0.01);
        QVERIFY(t.rowBg().alphaF() > 0.99);
        QCOMPARE(t.textMuted(), QColor("#a6a6b4"));                      // ungueltig -> Standard
        QCOMPARE(t.panelRadius(), 4);
        QCOMPARE(t.rowHeight(), 24);
        QCOMPARE(t.sans(), QStringLiteral("Inter"));                       // Schrift fehlt -> Standard
        QCOMPARE(t.extra().value("posBg").toString(), QStringLiteral("#14151a"));

        // Akzent aus den Settings gewinnt erst, wenn er vom Werkszustand abweicht
        t.applySettings(1.0, 0.0, "#e10600", "", "");
        QCOMPARE(t.accent(), QColor("#00aaff"));
        t.applySettings(1.0, 0.0, "#ff00ff", "", "");
        QCOMPARE(t.accent(), QColor("#ff00ff"));

        // Leeres Design = wieder exakt der alte Look
        t.applyDesign("kers", QJsonObject(), QString());
        t.applySettings(1.0, 0.0, "#e10600", "", "");
        QCOMPARE(t.panelBg(), panelVorher);
        QCOMPARE(t.accent(), QColor("#e10600"));
    }
};

QTEST_MAIN(TstDesign)
#include "tst_design.moc"
