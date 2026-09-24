// Updater ohne Netz: Tag-Erkennung, Versionsvergleich, Release-Liste aus einer
// gespeicherten GitHub-Antwort, Host-Allowlist, Aufraeumen, Tausch-Skript.
#include "updater.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace kers::app;

class TstUpdater : public QObject {
    Q_OBJECT
private slots:
    void tags()
    {
        QCOMPARE(versionAusTag("KERS_SubsystemsV0.2.6"), QStringLiteral("0.2.6"));
        QCOMPARE(versionAusTag("KERS_SubsystemsV.0.1.0"), QStringLiteral("0.1.0"));     // der Punkt zu viel
        QCOMPARE(versionAusTag("v0.3.0"), QString());
        QCOMPARE(versionAusTag(""), QString());
    }

    void zahlen()
    {
        QCOMPARE(alsZahlen("0.0.10"), (QList<int>{0, 0, 10}));
        QCOMPARE(alsZahlen("x.1"), (QList<int>{0, 0, 0}));
        QVERIFY(vergleiche(alsZahlen("0.0.10"), alsZahlen("0.0.9")) > 0);        // als Text waere es kleiner
        QVERIFY(vergleiche(alsZahlen("0.3.0"), alsZahlen("0.2.6")) > 0);
        QCOMPARE(vergleiche(alsZahlen("0.2.6"), alsZahlen("0.2.6")), 0);
        QVERIFY(vergleiche(alsZahlen("0.1.0"), Updater::ersteMitDatenordner()) < 0);
    }

    void releaseListe()
    {
        // Gespeicherte GitHub-Antwort, gekuerzt (ohne Rohstring - moc stolpert sonst darueber)
        auto asset = [](const char* name, double size, const char* digest = "", const char* url = "") {
            return QJsonObject{{"name", name}, {"size", size}, {"digest", digest}, {"browser_download_url", url}};
        };
        const QJsonArray rel{
            QJsonObject{{"tag_name", "KERS_SubsystemsV0.2.6"}, {"published_at", "2026-09-10T12:00:00Z"}, {"draft", false},
                        {"prerelease", false},
                        {"assets", QJsonArray{asset("KERS_Subsystems.exe", 254000000, "sha256:abc",
                                                    "https://github.com/KERSEX/KERS_Overlay/releases/download/x/KERS_Subsystems.exe")}}},
            QJsonObject{{"tag_name", "KERS_SubsystemsV.0.1.0"}, {"published_at", "2026-08-01T00:00:00Z"},
                        {"assets", QJsonArray{asset("KERS_Subsystems.exe", 10)}}},
            QJsonObject{{"tag_name", "KERS_SubsystemsV0.0.10"}, {"published_at", "2026-07-01T00:00:00Z"},
                        {"assets", QJsonArray{asset("KERS_Subsystems.exe", 5)}}},
            QJsonObject{{"tag_name", "KERS_SubsystemsV0.9.9"}, {"draft", true}, {"assets", QJsonArray{asset("KERS_Subsystems.exe", 1)}}},
            QJsonObject{{"tag_name", "KERS_SubsystemsV0.2.5"}, {"assets", QJsonArray{asset("anderes.zip", 1)}}},
            QJsonObject{{"tag_name", "irgendwas"}, {"assets", QJsonArray{asset("KERS_Subsystems.exe", 1)}}},
        };
        const QByteArray json = QJsonDocument(rel).toJson();
        const QList<ReleaseInfo> l = releasesAusJson(json);
        QCOMPARE(l.size(), 3);
        QCOMPARE(l.at(0).version, QStringLiteral("0.2.6"));
        QCOMPARE(l.at(0).size, qint64(254000000));
        QCOMPARE(l.at(0).digest, QStringLiteral("sha256:abc"));
        QCOMPARE(l.at(0).published, QStringLiteral("2026-09-10"));
        QCOMPARE(l.at(1).version, QStringLiteral("0.1.0"));
        QCOMPARE(l.at(2).version, QStringLiteral("0.0.10"));
        QVERIFY(releasesAusJson("kaputt").isEmpty());
    }

    void hosts()
    {
        QVERIFY(Updater::hostErlaubt("github.com"));
        QVERIFY(Updater::hostErlaubt("objects.githubusercontent.com"));
        QVERIFY(Updater::hostErlaubt("release-assets.githubusercontent.com"));
        QVERIFY(!Updater::hostErlaubt("github.com.evil.example"));
        QVERIFY(!Updater::hostErlaubt(""));
    }

    void aufraeumen()
    {
        QTemporaryDir dir;
        const QString exe = dir.filePath("KERS_Subsystems.exe");
        for (const QString& n : {exe, exe + ".bak", exe + ".new", dir.filePath("_kers_update.bat")}) {
            QFile f(n);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QStringList weg = altlastenAufraeumen(exe);
        QCOMPARE(weg.size(), 3);
        QVERIFY(QFile::exists(exe));                 // die laufende EXE bleibt natuerlich
        QVERIFY(!QFile::exists(exe + ".bak"));
        QVERIFY(altlastenAufraeumen(exe).isEmpty());
    }

    void tauschSkriptIstCrlf()
    {
        const QByteArray s = tauschSkript("C:/K/KERS_Subsystems.exe", "C:/K/KERS_Subsystems.exe.new",
                                          "C:/K/KERS_Subsystems.exe.bak", 4242);
        QVERIFY(!s.startsWith("\xEF\xBB\xBF"));
        QVERIFY(s.contains("PID eq 4242"));
        QVERIFY(s.contains("move /y \"C:\\K\\KERS_Subsystems.exe\" \"C:\\K\\KERS_Subsystems.exe.bak\""));
        QVERIFY(s.contains("start \"\" \"C:\\K\\KERS_Subsystems.exe\""));
        // jede Zeile mit CRLF - cmd.exe verschluckt sonst Zeichen am Zeilenanfang
        QCOMPARE(s.count('\n'), s.count("\r\n"));
    }
};

QTEST_GUILESS_MAIN(TstUpdater)
#include "tst_updater.moc"
