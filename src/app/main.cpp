// KERS Subsystems - Startpunkt. Portierung von hud/kers_hud.py.
//
// Zeigt das Overlay als IMMER-IM-VORDERGRUND-Fenster auf dem Desktop, dazu das
// Schaltbrett (Server, HUD an/aus, Bildschirm fuellen, sperren, OBS) und ein
// Tray-Icon - die Notbremse, wenn das HUD gesperrt und das Schaltbrett zu ist.
//
// Optionen:
//     --url http://127.0.0.1:5100   anderer Server (Remote-Betrieb ueber SSE)
//     --demo [race|quali]           erfundenes Rennen statt Server
//     --obs                         eigenes OBS-Fenster gleich mit oeffnen
//     --chroma                      HUD-Fenster einfarbig statt durchsichtig
//     --unlocked                    entsperrt starten
//     --no-panel                    ohne Schaltbrett starten
//     --hz N                        Overlay-Renderrate (10/20/30/60/120)
//     --qml-dir <Ordner>            Szene aus Dateien statt aus der EXE (Bauen am QML)
//     --shot <png> [--shot-delay ms] Szene nach der Wartezeit als Bild sichern und beenden
//     --server                      nur der Server (UDP + HTTP), ohne HUD - mit Tray-Icon
//     --no-server                   den eingebauten Server nicht automatisch starten
//     --port N / --udp-port N       Ports des Servers (Standard 5100 / 20777)
//
// Ohne --url laeuft der Server IM SELBEN PROZESS: das HUD bekommt seine Daten
// direkt (LocalFeed), Browser, Handy und OBS weiter ueber HTTP.
//
// Log: data\kers.log neben der EXE.

#include "bridge.h"
#include "data_source.h"
#include "hud_state.h"
#include "hud_window.h"
#include "icon.h"
#include "kers_server.h"
#include "local_link.h"
#include "log.h"
#include "obs_window.h"
#include "paths.h"
#include "server_control.h"
#include "subsystems_panel.h"
#include "theme.h"
#include "updater.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>
#include <QSslSocket>
#include <QSurfaceFormat>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QDebug>

#include <memory>

#ifdef Q_OS_WIN
#include <shobjidl.h>
#endif

using namespace kers;
using kers::hud::HudWindow;
using kers::hud::ObsWindow;
using kers::app::SubsystemsPanel;

namespace {

struct Args {
    QString url;
    QString demo;          // "", "race", "quali"
    bool obs = false;
    bool chroma = false;
    bool unlocked = false;
    bool noPanel = false;
    int hz = 0;
    QString qmlDir;
    QString shot;
    int shotDelay = 3000;
    bool server = false;
    bool noServer = false;
    int port = 5100;
    int udpPort = 20777;
};

Args parseArgs(const QStringList& a)
{
    Args out;
    for (int i = 1; i < a.size(); ++i) {
        const QString& s = a.at(i);
        auto next = [&](QString& ziel) { if (i + 1 < a.size()) ziel = a.at(++i); };
        if (s == QLatin1String("--url")) next(out.url);
        else if (s == QLatin1String("--demo")) {
            out.demo = QStringLiteral("race");
            if (i + 1 < a.size() && (a.at(i + 1) == QLatin1String("race") || a.at(i + 1) == QLatin1String("quali")))
                out.demo = a.at(++i);
        }
        else if (s == QLatin1String("--obs")) out.obs = true;
        else if (s == QLatin1String("--chroma")) out.chroma = true;
        else if (s == QLatin1String("--unlocked")) out.unlocked = true;
        else if (s == QLatin1String("--no-panel")) out.noPanel = true;
        else if (s == QLatin1String("--hz")) { QString v; next(v); out.hz = v.toInt(); }
        else if (s == QLatin1String("--qml-dir")) next(out.qmlDir);
        else if (s == QLatin1String("--shot")) next(out.shot);
        else if (s == QLatin1String("--shot-delay")) { QString v; next(v); out.shotDelay = v.toInt(); }
        else if (s == QLatin1String("--server")) out.server = true;
        else if (s == QLatin1String("--no-server")) out.noServer = true;
        else if (s == QLatin1String("--port")) { QString v; next(v); out.port = v.toInt(); }
        else if (s == QLatin1String("--udp-port")) { QString v; next(v); out.udpPort = v.toInt(); }
    }
    return out;
}

/// Wo die Python-Fassung (0.2.x) ihren Server entpackt hatte und laufen liess:
/// data\main.exe, vor dem Umzug nach data\ direkt neben der EXE. Laeuft dort nach
/// dem Update noch einer, haelt er die Ports - siehe app::alteServerBeenden.
QStringList alteServerPfade()
{
    return {paths::dataFile(QStringLiteral("main.exe")),
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("main.exe"))};
}

/// Eigene Identitaet in der Taskleiste - sonst gruppiert Windows nach dem
/// Programm, das gestartet hat.
void claimTaskbarIdentity()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"KERS.Overlay.HUD");
#endif
}

/// Tray-Menue - die Notbremse, wenn das HUD gesperrt und das Schaltbrett zu ist.
QSystemTrayIcon* buildTray(QApplication& app, HudWindow& hud, HudState& state, SubsystemsPanel* panel)
{
    auto* tray = new QSystemTrayIcon(app::makeIcon(), &app);
    tray->setToolTip(QStringLiteral("KERS HUD"));
    auto* menu = new QMenu();

    QAction* showPanel = menu->addAction(QStringLiteral("Subsystems anzeigen"));
    QObject::connect(showPanel, &QAction::triggered, [panel, &state]() {
        if (!panel)
            return;
        panel->show();
        panel->raise();
        panel->activateWindow();
        state.panelVisible = true;
    });
    showPanel->setEnabled(panel != nullptr);
    if (panel) {
        menu->addAction(QStringLiteral("Start Server"), panel, &SubsystemsPanel::startServer);
        menu->addAction(QStringLiteral("Stop Server"), panel, &SubsystemsPanel::stopServer);
    }
    menu->addSeparator();

    QAction* show = menu->addAction(QStringLiteral("HUD anzeigen"));
    show->setCheckable(true);
    show->setChecked(state.visible);
    QObject::connect(show, &QAction::triggered, &hud, &HudWindow::setHudVisible);
    QObject::connect(&hud, &HudWindow::hudVisibilityChanged, show, &QAction::setChecked);

    QAction* lock = menu->addAction(QStringLiteral("Gesperrt (klick-durch)"));
    lock->setCheckable(true);
    lock->setChecked(state.locked);
    QObject::connect(lock, &QAction::triggered, &hud, &HudWindow::setLocked);
    QObject::connect(&hud, &HudWindow::hudLockedChanged, lock, &QAction::setChecked);

    QAction* obs = menu->addAction(QStringLiteral("OBS-Fenster anzeigen"));
    obs->setCheckable(true);
    obs->setChecked(state.obsWindow);
    obs->setEnabled(panel != nullptr);
    QObject::connect(obs, &QAction::triggered, [panel](bool on) { if (panel) panel->setObsWindow(on); });

    QAction* find = menu->addAction(QStringLiteral("OBS: HUD-Fenster auffindbar machen"));
    find->setCheckable(true);
    find->setChecked(state.obsFindable);
    QObject::connect(find, &QAction::triggered, &hud, &HudWindow::setFindable);

    QAction* chroma = menu->addAction(QStringLiteral("OBS: HUD-Fenster einfarbig (Chroma-Key)"));
    chroma->setCheckable(true);
    chroma->setChecked(state.obsChroma);
    QObject::connect(chroma, &QAction::triggered, [&hud](bool on) { hud.setChroma(on); });

    menu->addSeparator();
    if (panel)
        menu->addAction(QStringLiteral("Bildschirm fuellen"), panel, &SubsystemsPanel::fillScreen);
    menu->addAction(QStringLiteral("Neu laden"), &hud, &HudWindow::reloadPage);
    menu->addSeparator();
    if (panel)
        menu->addAction(QStringLiteral("Alles beenden (mit Server)"), panel, &SubsystemsPanel::shutdownAll);
    menu->addAction(QStringLiteral("Nur HUD beenden"), &app, &QApplication::quit);

    tray->setContextMenu(menu);
    // Linksklick aufs Icon = schnell sperren/entsperren, ohne Menue
    QObject::connect(tray, &QSystemTrayIcon::activated, [&hud](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            hud.toggleLocked();
    });
    tray->show();
    return tray;
}

/// --server: nur der Server, ohne HUD. Ein Tray-Icon zum Oeffnen der Seiten und Beenden.
int runServerOnly(QApplication& app, const Args& args)
{
    server::KersServer srv(paths::dataDir());
    const QStringList weg = app::alteServerBeenden(alteServerPfade());
    if (!weg.isEmpty())
        qInfo().noquote() << "[UPDATE] Server der Python-Fassung beendet:" << weg.join(QStringLiteral(", "));
    if (!srv.start(static_cast<quint16>(args.port), static_cast<quint16>(args.udpPort))) {
        QMessageBox::critical(nullptr, QStringLiteral("KERS Server"), srv.errorString());
        return 1;
    }
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(srv.httpPort());
    auto* tray = new QSystemTrayIcon(app::makeIcon(), &app);
    tray->setToolTip(QStringLiteral("KERS Server - HTTP %1, UDP %2").arg(srv.httpPort()).arg(srv.udpPort()));
    auto* menu = new QMenu();
    menu->addAction(QStringLiteral("Regie oeffnen"), [base] { QDesktopServices::openUrl(QUrl(base + QStringLiteral("/regie"))); });
    menu->addAction(QStringLiteral("Settings oeffnen"), [base] { QDesktopServices::openUrl(QUrl(base + QStringLiteral("/settings"))); });
    menu->addAction(QStringLiteral("Overlay oeffnen"), [base] { QDesktopServices::openUrl(QUrl(base + QStringLiteral("/"))); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Server beenden"), &app, &QApplication::quit);
    tray->setContextMenu(menu);
    tray->show();
    const int rc = app.exec();
    srv.stop();
    delete menu;
    return rc;
}

}  // namespace

int main(int argc, char* argv[])
{
    // Muss VOR der QApplication stehen: ohne Alphapuffer bleibt der Hintergrund
    // schwarz statt durchsichtig.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    claimTaskbarIdentity();
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KERS Subsystems"));
    app.setOrganizationName(QStringLiteral("KERS"));
    app.setApplicationVersion(paths::appVersion());
    app.setWindowIcon(app::makeIcon());
    // Ohne das beendet sich alles, sobald HUD und Schaltbrett einmal zu sind.
    app.setQuitOnLastWindowClosed(false);

    // ⚠ Vor allem anderen: alte Dateien neben der EXE nach data\ holen, sonst
    // liest hud_state.json unten die Vorgaben statt des echten Stands.
    const QStringList umgezogen = paths::migrateData();
    log::install(paths::dataFile(QStringLiteral("kers.log")));
    qInfo() << "KERS Subsystems" << paths::appVersion() << "- Qt" << qVersion()
            << "- Daten:" << paths::dataDir() << "- SSL:" << QSslSocket::supportsSsl();
    if (!umgezogen.isEmpty())
        qInfo().noquote() << "[HUD] Nach data/ verschoben:" << umgezogen.join(QStringLiteral(", "));
    // Reste eines Updates wegraeumen (.bak/.new/_kers_update.bat) - dass dieser
    // Code laeuft, beweist, dass die neue Fassung startet.
    const QStringList weg = app::altlastenAufraeumen(QCoreApplication::applicationFilePath());
    if (!weg.isEmpty())
        qInfo().noquote() << "[UPDATE] Aufgeraeumt:" << weg.join(QStringLiteral(", "));

    const Args args = parseArgs(app.arguments());
    if (args.server)
        return runServerOnly(app, args);
    const QString stateFile = paths::dataFile(QStringLiteral("hud_state.json"));
    HudState state = HudState::load(stateFile);
    if (!args.url.isEmpty())
        state.baseUrl = args.url;
    if (args.unlocked)
        state.locked = false;
    if (args.hz > 0)
        state.hz = args.hz;
    if (args.chroma)
        state.obsChroma = true;
    if (args.obs)
        state.obsWindow = true;

    hud::loadFonts();
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qWarning() << "[HUD] Kein System-Tray gefunden.";

    // Der eingebaute Server. Mit --url wird stattdessen ein fremder beobachtet.
    const bool lokal = args.url.isEmpty();
    // Fremder Server mit Code (ab 0.3.1): der einmal geholte Schluessel geht bei
    // jeder Anfrage mit.
    if (!lokal && !state.remoteKey.isEmpty())
        hud::setAccessKey(state.remoteKey.toLatin1());
    server::KersServer kersServer(paths::dataDir());
    std::unique_ptr<app::ServerControl> serverCtl;
    if (lokal) {
        auto local = std::make_unique<app::LocalServer>(kersServer, static_cast<quint16>(args.port),
                                                        static_cast<quint16>(args.udpPort));
        local->setAlteServer(alteServerPfade());
        serverCtl = std::move(local);
    } else {
        auto remote = std::make_unique<app::RemoteStatus>(state.baseUrl);
        remote->startPolling();
        serverCtl = std::move(remote);
    }
    app::ServerControl& server = *serverCtl;

    HudWindow hud(state, args.demo, args.qmlDir);
    if (lokal) {
        // Das HUD haengt direkt am Server im Prozess - kein HTTP, kein SSE.
        hud.bridge()->setDataSource(new app::LocalDataSource(&kersServer, hud.bridge()));
        hud.bridge()->setLiveFeedFactory([&kersServer, hz = state.hz](QObject* parent) -> hud::Feed* {
            return new app::LocalFeed(&kersServer, hz, parent);
        });
        // Beim Demo laeuft kein Spiel - dann bleibt der Server aus, bis man ihn startet.
        if (state.serverAutostart && !args.noServer && args.demo.isEmpty())
            server.start();
    }
    hud.start();                              // Datenstrom oeffnen (lokal, SSE bzw. Demo)

    // Zweites Fenster fuer die Aufnahme - teilt sich die Bruecke mit dem HUD.
    ObsWindow* obs = nullptr;
    auto obsAccessor = [&](bool ensure) -> ObsWindow* {
        if (!obs && ensure)
            obs = new ObsWindow(state, hud.bridge(), args.qmlDir);
        return obs;
    };

    SubsystemsPanel* panel = nullptr;
    if (!args.noPanel) {
        panel = new SubsystemsPanel(state, hud, server, obsAccessor);
        // Das Schaltbrett geht IMMER auf. Zumachen waehrend der Sitzung blendet nur aus.
        panel->show();
        state.panelVisible = true;
        if (state.obsWindow)
            panel->setObsWindow(true);
    } else if (state.obsWindow) {
        obsAccessor(true)->show();
    }

    QSystemTrayIcon* tray = buildTray(app, hud, state, panel);
    QObject::connect(&server, &app::ServerControl::errorOccurred, tray, [tray](const QString& msg) {
        tray->showMessage(QStringLiteral("KERS Server startet nicht"), msg, QSystemTrayIcon::Warning, 8000);
    });
    if (lokal) {
        // Zugriff: wer klopft an, wer ist neu dazugekommen - sichtbar, auch wenn
        // das Schaltbrett zu ist.
        QObject::connect(&kersServer.access(), &server::AccessGuard::attemptSeen, tray,
                         [tray](const QString& ip, const QString& agent) {
                             tray->showMessage(QStringLiteral("Fremdes Gerät ohne Code"),
                                               QStringLiteral("%1 (%2) will auf KERS zugreifen. Den Code findest du "
                                                              "im Schaltbrett unter „Zugriff“.")
                                                   .arg(ip, server::AccessGuard::deviceNameFromAgent(agent)),
                                               QSystemTrayIcon::Information, 6000);
                         });
        QObject::connect(&kersServer.access(), &server::AccessGuard::deviceAdded, tray,
                         [tray](const QString& name, const QString& ip) {
                             tray->showMessage(QStringLiteral("Gerät freigegeben"),
                                               QStringLiteral("%1 (%2) hat den Code eingegeben.").arg(name, ip),
                                               QSystemTrayIcon::Information, 5000);
                         });
    }
    if (lokal && !kersServer.isRunning() && !kersServer.errorString().isEmpty())
        tray->showMessage(QStringLiteral("KERS Server startet nicht"), kersServer.errorString(), QSystemTrayIcon::Warning, 8000);
    tray->showMessage(QStringLiteral("KERS HUD laeuft"),
                      QStringLiteral("Rechtsklick auf das Tray-Icon oeffnet die Subsystems."),
                      app::makeIcon(), 4000);

    if (!args.shot.isEmpty()) {
        // ⚠ Das Fenster muss dafuer wirklich auf dem Schirm sein - offscreen
        // gibt es keine Schrift.
        QTimer::singleShot(args.shotDelay, &hud, [&hud, panel, shot = args.shot]() {
            const QImage img = hud.grabWindow();
            qInfo() << "[HUD] Bild gesichert:" << shot << img.save(shot) << img.size();
            if (panel) {
                QString pshot = shot;
                pshot.replace(QStringLiteral(".png"), QStringLiteral("-panel.png"));
                qInfo() << "[HUD] Schaltbrett gesichert:" << pshot << panel->grab().save(pshot);
            }
            QCoreApplication::quit();
        });
    }

    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        if (auto* remote = qobject_cast<app::RemoteStatus*>(&server))
            remote->stopPolling();
        hud.stop();
        kersServer.stop();
        if (obs)
            obs->hide();
        if (panel)
            state.panelVisible = panel->isVisible();
        state.save(stateFile);
    });
    const int rc = app.exec();
    delete panel;
    return rc;
}
