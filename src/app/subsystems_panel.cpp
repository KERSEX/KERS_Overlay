#include "subsystems_panel.h"

#include "log_view.h"

#include "icon.h"
#include "paths.h"

#include <QApplication>
#include <QEvent>
#include <QCompleter>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QDebug>

namespace kers::app {

namespace {

const char* const STYLE = R"(
QWidget        { background: #0e0e13; color: #ffffff; font-family: 'Segoe UI'; font-size: 12px; }
QGroupBox      { border: 1px solid rgba(255,255,255,0.09); border-radius: 8px;
                 margin-top: 14px; padding: 12px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px;
                 color: #e10600; font-weight: bold; letter-spacing: 1px; }
QCheckBox      { padding: 2px 0; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 3px;
                 border: 1px solid rgba(255,255,255,0.25); background: #1b1b22; }
QCheckBox::indicator:checked { background: #e10600; border-color: #e10600; }
QPushButton    { background: #1b1b22; border: 1px solid rgba(255,255,255,0.12);
                 border-radius: 6px; padding: 6px 10px; }
QPushButton:hover  { background: #27272f; }
QPushButton:pressed { background: #e10600; }
QPushButton:disabled { background: #14141a; color: #6a6a78;
                 border-color: rgba(255,255,255,0.06); }
QComboBox, QSpinBox { background: #1b1b22; border: 1px solid rgba(255,255,255,0.12);
                 border-radius: 6px; padding: 4px 6px; }
QComboBox QAbstractItemView { background: #1b1b22; selection-background-color: #e10600; }
QLabel#hint    { color: #a6a6b4; }
QLabel#head    { font-size: 15px; font-weight: bold; letter-spacing: 2px; }
)";

// Der grosse Hauptschalter faellt bewusst aus dem restlichen Stil heraus.
const char* const POWER_ON = R"(
QPushButton { background: #14361f; border: 1px solid #3ddc84; border-radius: 8px;
              color: #3ddc84; font-size: 15px; font-weight: bold; letter-spacing: 2px;
              padding: 12px; }
QPushButton:hover { background: #1a4529; }
)";
const char* const POWER_OFF = R"(
QPushButton { background: #2a1113; border: 1px solid #e10600; border-radius: 8px;
              color: #ff5f5a; font-size: 15px; font-weight: bold; letter-spacing: 2px;
              padding: 12px; }
QPushButton:hover { background: #371417; }
)";
// "Alles beenden" beendet auch den Server - das soll man sehen, bevor man klickt.
const char* const DANGER = R"(
QPushButton { background: #1b1b22; border: 1px solid #e10600; border-radius: 6px;
              color: #ff5f5a; font-weight: bold; letter-spacing: 1px; padding: 8px; }
QPushButton:hover { background: #2a1113; }
)";

struct HzChoice {
    int value;
    const char* label;
};
const HzChoice kHzChoices[] = {
    {10, "10 Hz (sparsam)"}, {20, "20 Hz"}, {30, "30 Hz (Standard)"}, {60, "60 Hz"}, {120, "120 Hz"},
};

QFrame* hline()
{
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("background: rgba(255,255,255,0.09); max-height: 1px;"));
    return line;
}

}  // namespace

QLabel* SubsystemsPanel::dot(const QString& color)
{
    auto* lbl = new QLabel();
    lbl->setFixedSize(10, 10);
    lbl->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(color));
    return lbl;
}

void SubsystemsPanel::setChecked(QAbstractButton* b, bool v)
{
    b->blockSignals(true);
    b->setChecked(v);
    b->blockSignals(false);
}

SubsystemsPanel::SubsystemsPanel(HudState& state, hud::HudWindow& hud, ServerControl& server, ObsAccessor obs)
    : QWidget(nullptr)
    , m_state(state)
    , m_hud(hud)
    , m_server(server)
    , m_obs(std::move(obs))
{
    setWindowTitle(QStringLiteral("KERS Subsystems"));
    setWindowIcon(makeIcon());
    setStyleSheet(QString::fromLatin1(STYLE));
    setMinimumWidth(460);
    resize(500, 560);

    m_startTimeout.setSingleShot(true);
    m_startTimeout.setInterval(25000);   // so lange gilt der Server als "startet gerade"
    connect(&m_startTimeout, &QTimer::timeout, this, [this]() {
        m_starting = false;
        m_btnServer->setEnabled(true);
        m_btnServer->setText(QStringLiteral("Start Server (letzter Versuch lief nicht an)"));
    });

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);
    outer->addWidget(buildHeader());
    outer->addWidget(buildPower());
    outer->addWidget(buildHudGroup());
    outer->addWidget(buildObsGroup());
    outer->addStretch(1);
    outer->addWidget(buildFooter());

    connect(&m_server, &ServerControl::statusChanged, this, &SubsystemsPanel::applyStatus);
    connect(&m_server, &ServerControl::errorOccurred, this, &SubsystemsPanel::serverFehler);
    // ⚠ Der Server ist beim Autostart schon hochgefahren, bevor es dieses Fenster
    // gab - die Meldung davon ist verpufft. Also den Stand jetzt einmal abholen.
    m_server.announce();

    // Update-Suche: einmal beim Start still nachsehen - nur melden, nie von selbst
    // laden. Die Liste aller Fassungen versetzt: GitHub zaehlt ohne Anmeldung
    // nur 60 Anfragen pro Stunde.
    connect(&m_updater, &Updater::updateAvailable, this, &SubsystemsPanel::updateGefunden);
    connect(&m_updater, &Updater::upToDate, this, &SubsystemsPanel::updateAktuell);
    connect(&m_updater, &Updater::failed, this, &SubsystemsPanel::updateFehler);
    connect(&m_updater, &Updater::progress, this, &SubsystemsPanel::updateFortschritt);
    connect(&m_updater, &Updater::ready, this, &SubsystemsPanel::updateBereit);
    connect(&m_updater, &Updater::releasesGeladen, this, &SubsystemsPanel::releasesDa);
    QTimer::singleShot(3000, &m_updater, &Updater::check);
    QTimer::singleShot(4500, &m_updater, &Updater::releasesLaden);

    // HUD -> Oberflaeche (z.B. wenn das Fenster mit der Maus verschoben wird)
    connect(&m_hud, &hud::HudWindow::hudGeometryChanged, this, &SubsystemsPanel::applyGeometry);
    connect(&m_hud, &hud::HudWindow::hudLockedChanged, this, [this](bool v) { setChecked(m_cbLocked, v); });
    connect(&m_hud, &hud::HudWindow::hudVisibilityChanged, this, &SubsystemsPanel::applyPower);
    connect(&m_hud, &hud::HudWindow::hudLayoutEditChanged, this, &SubsystemsPanel::applyLayoutEdit);

    move(state.panelX, state.panelY);
    if (state.panelOnTop)
        setOnTop(true);
}

// ── Kopf: Status + Server starten ────────────────────────────────────────────
QWidget* SubsystemsPanel::buildHeader()
{
    auto* wrap = new QWidget();
    auto* box = new QVBoxLayout(wrap);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);

    auto* title = new QHBoxLayout();
    title->setSpacing(8);
    auto* bolt = new QLabel();
    bolt->setPixmap(makeIcon().pixmap(34, 34));    // kein setFixedSize: der Blitz ist hochkant
    title->addWidget(bolt);
    auto* head = new QLabel(QStringLiteral("KERS SUBSYSTEMS"));
    head->setObjectName(QStringLiteral("head"));
    title->addWidget(head);
    title->addStretch(1);
    box->addLayout(title);

    auto* row = new QHBoxLayout();
    row->setSpacing(6);
    m_dotServer = dot(QStringLiteral("#e10600"));
    m_lblServer = new QLabel(QStringLiteral("Server ..."));
    m_dotUdp = dot(QStringLiteral("#e10600"));
    m_lblUdp = new QLabel(QStringLiteral("UDP ..."));
    row->addWidget(m_dotServer);
    row->addWidget(m_lblServer);
    row->addSpacing(10);
    row->addWidget(m_dotUdp);
    row->addWidget(m_lblUdp);
    row->addStretch(1);
    m_lblVersion = new QLabel(QStringLiteral("v") + paths::appVersion());
    m_lblVersion->setObjectName(QStringLiteral("hint"));
    m_lblVersion->setToolTip(QStringLiteral("Version dieser Anwendung"));
    row->addWidget(m_lblVersion);
    box->addLayout(row);

    m_btnUpdate = new QPushButton();
    m_btnUpdate->hide();
    connect(m_btnUpdate, &QPushButton::clicked, this, &SubsystemsPanel::updateKnopf);
    box->addWidget(m_btnUpdate);

    auto* srv = new QHBoxLayout();
    m_btnServer = new QPushButton(QStringLiteral("Start Server"));
    m_btnServer->setToolTip(QStringLiteral("Startet den Server (UDP 20777, Web auf %1).").arg(m_server.baseUrl()));
    connect(m_btnServer, &QPushButton::clicked, this, &SubsystemsPanel::startServer);
    m_btnStop = new QPushButton(QStringLiteral("Stop Server"));
    m_btnStop->setEnabled(false);
    connect(m_btnStop, &QPushButton::clicked, this, &SubsystemsPanel::stopServer);
    if (!m_server.canStart()) {
        m_btnServer->setEnabled(false);
        m_btnServer->setToolTip(QStringLiteral("Der Server laeuft woanders (%1) und wird von hier nur beobachtet.").arg(m_server.baseUrl()));
        m_btnStop->setToolTip(m_btnServer->toolTip());
    }
    srv->addWidget(m_btnServer);
    srv->addWidget(m_btnStop);
    box->addLayout(srv);
    box->addWidget(hline());
    return wrap;
}

void SubsystemsPanel::startServer()
{
    if (!m_server.canStart())
        return;
    m_starting = true;
    m_btnServer->setEnabled(false);
    m_btnServer->setText(QStringLiteral("Server Starting ..."));
    m_startTimeout.start();
    m_server.start();
    m_server.refresh();
}

void SubsystemsPanel::stopServer()
{
    if (!m_server.canStart())
        return;
    m_server.stop();
    m_starting = false;
    m_startTimeout.stop();
    m_btnStop->setEnabled(false);
    QTimer::singleShot(400, &m_server, [this]() { m_server.refresh(); });
}

void SubsystemsPanel::shutdownAll()
{
    // Server beenden, HUD schliessen, Subsystems beenden - nach einer Rueckfrage.
    const QString what = (m_server.canStart() && m_server.running())
        ? QStringLiteral("Der Server und das HUD werden beendet.")
        : QStringLiteral("Das HUD wird beendet.");
    const auto antwort = QMessageBox::question(this, QStringLiteral("Alles beenden"),
                                               what + QStringLiteral("\n\nWeitermachen?"));
    if (antwort != QMessageBox::Yes)
        return;
    if (m_server.canStart() && m_server.running())
        m_server.stop();
    QApplication::quit();
}

// ── Hauptschalter + An Bildschirm anpassen ───────────────────────────────────
QWidget* SubsystemsPanel::buildPower()
{
    auto* wrap = new QWidget();
    auto* lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_btnPower = new QPushButton();
    m_btnPower->setCheckable(true);
    m_btnPower->setChecked(m_state.visible);
    connect(m_btnPower, &QPushButton::clicked, &m_hud, &hud::HudWindow::setHudVisible);
    lay->addWidget(m_btnPower);
    applyPower(m_state.visible);

    auto* row = new QHBoxLayout();
    auto* btnFill = new QPushButton(QStringLiteral("An Bildschirm anpassen"));
    btnFill->setToolTip(QStringLiteral(
        "Setzt das HUD auf die volle Groesse des Bildschirms, auf dem es gerade liegt,\n"
        "und sperrt es dabei (klick-durch) - sonst laege eine bildschirmfuellende\n"
        "Bearbeiten-Flaeche ueber allem und man kaeme an nichts mehr heran."));
    connect(btnFill, &QPushButton::clicked, this, &SubsystemsPanel::fillScreen);
    auto* btnCenter = new QPushButton(QStringLiteral("Zentrieren"));
    connect(btnCenter, &QPushButton::clicked, this, &SubsystemsPanel::centerHud);
    auto* btnReload = new QPushButton(QStringLiteral("Neu laden"));
    connect(btnReload, &QPushButton::clicked, &m_hud, &hud::HudWindow::reloadPage);
    row->addWidget(btnFill, 2);
    row->addWidget(btnCenter, 1);
    row->addWidget(btnReload, 1);
    lay->addLayout(row);

    m_btnLayout = new QPushButton(QStringLiteral("Layout bearbeiten"));
    m_btnLayout->setCheckable(true);
    m_btnLayout->setToolTip(QStringLiteral(
        "Bausteine mit der Maus an ihren Platz ziehen.\n\n"
        "Entsperrt das HUD dabei automatisch - gesperrt gehen alle Klicks\n"
        "durch das Fenster hindurch und die Szene bekaeme gar keine Maus.\n"
        "Beendet wird IN der Szene (Fertig-Knopf oben oder Esc): das\n"
        "entsperrte HUD liegt bildschirmgross ueber diesem Fenster.\n"
        "Danach wird der vorherige Sperrzustand wiederhergestellt."));
    connect(m_btnLayout, &QPushButton::toggled, this, &SubsystemsPanel::layoutEditUmschalten);
    auto* btnReset = new QPushButton(QStringLiteral("Standard Layout"));
    btnReset->setToolTip(QStringLiteral(
        "Alle Bausteine zurueck auf ihren einprogrammierten Platz.\n\n"
        "Betrifft NUR die Positionen (die Einstellung 'layout'), nicht\n"
        "Branding, Farben oder die uebrigen Einstellungen."));
    connect(btnReset, &QPushButton::clicked, this, &SubsystemsPanel::layoutZuruecksetzen);
    auto* zeile = new QHBoxLayout();
    zeile->addWidget(m_btnLayout, 2);
    zeile->addWidget(btnReset, 1);
    lay->addLayout(zeile);
    return wrap;
}

void SubsystemsPanel::layoutEditUmschalten(bool on)
{
    m_hud.setLayoutEdit(on);
    beschrifteLayoutKnopf(on);
}

void SubsystemsPanel::layoutZuruecksetzen()
{
    // Ein leeres `layout` heisst "kein Eintrag" - dann gilt in Overlay.qml wieder
    // der einprogrammierte Ausdruck (vier Bausteine rechnen dynamisch).
    const auto antwort = QMessageBox::question(
        this, QStringLiteral("Standard Layout"),
        QStringLiteral("Alle Bausteine wieder an ihren einprogrammierten Platz stellen?\n\n"
                       "Das verwirft jede Verschiebung, die du gemacht hast. Andere\n"
                       "Einstellungen bleiben unberuehrt."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (antwort != QMessageBox::Yes)
        return;
    m_hud.layoutZuruecksetzen();
}

void SubsystemsPanel::applyLayoutEdit(bool on)
{
    // HUD -> Oberflaeche: in der Szene auf Fertig geklickt (oder Esc).
    setChecked(m_btnLayout, on);
    beschrifteLayoutKnopf(on);
}

void SubsystemsPanel::beschrifteLayoutKnopf(bool on)
{
    m_btnLayout->setText(on ? QStringLiteral("Layout bearbeiten - FERTIG") : QStringLiteral("Layout bearbeiten"));
    m_btnLayout->setStyleSheet(on ? QString::fromLatin1(DANGER) : QString());
}

void SubsystemsPanel::applyPower(bool on)
{
    setChecked(m_btnPower, on);
    m_btnPower->setText(on ? QStringLiteral("HUD IST AN") : QStringLiteral("HUD IST AUS"));
    m_btnPower->setStyleSheet(QString::fromLatin1(on ? POWER_ON : POWER_OFF));
}

void SubsystemsPanel::fillScreen()
{
    // Auf den Bildschirm legen, auf dem das HUD gerade steht - und sperren:
    // unverriegelt laege jetzt eine bildschirmgrosse Klickflaeche ueber allem.
    QScreen* screen = m_hud.screen() ? m_hud.screen() : this->screen();
    const QRect geo = screen->geometry();
    m_hud.applyGeometry(geo.x(), geo.y(), geo.width(), geo.height());
    m_hud.setLocked(true);
    if (!m_state.visible)
        m_hud.setHudVisible(true);
}

void SubsystemsPanel::centerHud()
{
    QScreen* screen = m_hud.screen() ? m_hud.screen() : this->screen();
    const QRect area = screen->availableGeometry();
    const int w = m_hud.width(), h = m_hud.height();
    m_hud.applyGeometry(area.x() + (area.width() - w) / 2, area.y() + (area.height() - h) / 2, w, h);
}

// ── HUD-Fenster: platzieren ──────────────────────────────────────────────────
QWidget* SubsystemsPanel::buildHudGroup()
{
    auto* group = new QGroupBox(QStringLiteral("HUD-FENSTER"));
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(6);

    m_cbLocked = new QCheckBox(QStringLiteral("Gesperrt - Klicks gehen durch zum Spiel"));
    m_cbLocked->setChecked(m_state.locked);
    connect(m_cbLocked, &QCheckBox::toggled, &m_hud, &hud::HudWindow::setLocked);
    lay->addWidget(m_cbLocked);

    auto* hint = new QLabel(QStringLiteral("Entsperrt = roter Rahmen, Fenster laesst sich ziehen und skalieren."));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("Overlay-Hz")));
    m_cmbHz = new QComboBox();
    m_cmbHz->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cmbHz->setMinimumContentsLength(14);
    m_cmbHz->setToolTip(QStringLiteral(
        "Wie oft das Overlay hoechstens neu zeichnet. Gilt nur fuers HUD -\n"
        "OBS und Handy bleiben bei voller Rate."));
    for (const HzChoice& c : kHzChoices)
        m_cmbHz->addItem(QString::fromLatin1(c.label), c.value);
    const int idx = m_cmbHz->findData(m_state.hz);
    m_cmbHz->setCurrentIndex(idx >= 0 ? idx : 2);
    connect(m_cmbHz, &QComboBox::currentIndexChanged, this, [this]() { m_hud.setHz(m_cmbHz->currentData().toInt()); });
    row->addWidget(m_cmbHz, 1);
    lay->addLayout(row);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    m_spX = spin(-10000, 10000, m_state.x);
    m_spY = spin(-10000, 10000, m_state.y);
    m_spW = spin(240, 10000, m_state.w);
    m_spH = spin(160, 10000, m_state.h);
    const struct { const char* cap; QSpinBox* sp; } cols[] = {{"X", m_spX}, {"Y", m_spY}, {"Breite", m_spW}, {"Hoehe", m_spH}};
    for (int col = 0; col < 4; ++col) {
        grid->addWidget(new QLabel(QString::fromLatin1(cols[col].cap)), 0, col);
        grid->addWidget(cols[col].sp, 1, col);
        connect(cols[col].sp, &QSpinBox::valueChanged, this, &SubsystemsPanel::onGeometrySpin);
    }
    lay->addLayout(grid);

    m_cbOnTop = new QCheckBox(QStringLiteral("Dieses Fenster immer im Vordergrund"));
    m_cbOnTop->setChecked(m_state.panelOnTop);
    connect(m_cbOnTop, &QCheckBox::toggled, this, &SubsystemsPanel::setOnTop);
    lay->addWidget(m_cbOnTop);
    return group;
}

// ── OBS ──────────────────────────────────────────────────────────────────────
QWidget* SubsystemsPanel::buildObsGroup()
{
    auto* group = new QGroupBox(QStringLiteral("OBS"));
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(6);

    auto* hint = new QLabel(QStringLiteral(
        u"Empfohlen: das OBS-Fenster einschalten und es in OBS per Fensteraufnahme "
        u"holen („KERS OBS“), dazu einen Farbschluessel-Filter auf Magenta. "
        u"Das HUD-Fenster selbst friert in der Fensteraufnahme auf einem Standbild ein."));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_cbObsWindow = new QCheckBox(QStringLiteral("Eigenes OBS-Fenster"));
    m_cbObsWindow->setChecked(m_state.obsWindow);
    m_cbObsWindow->setToolTip(QStringLiteral(
        "Oeffnet ein zweites, UNDURCHSICHTIGES Fenster in Canvas-Groesse mit\n"
        "derselben Szene. Nur dieses laesst sich zuverlaessig per Fensteraufnahme\n"
        "holen. Das HUD auf dem Desktop kann dabei ruhig aus bleiben."));
    connect(m_cbObsWindow, &QCheckBox::toggled, this, &SubsystemsPanel::setObsWindow);
    lay->addWidget(m_cbObsWindow);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("Canvas")));
    m_spObsW = spin(320, 10000, m_state.obsW > 0 ? m_state.obsW : 1920);
    m_spObsH = spin(180, 10000, m_state.obsH > 0 ? m_state.obsH : 1080);
    for (QSpinBox* sp : {m_spObsW, m_spObsH}) {
        sp->setToolTip(QStringLiteral("Groesse des OBS-Fensters - am besten die OBS-Canvas."));
        connect(sp, &QSpinBox::valueChanged, this, &SubsystemsPanel::onObsCanvas);
        row->addWidget(sp);
    }
    row->addStretch(1);
    lay->addLayout(row);
    lay->addWidget(hline());

    auto* hint2 = new QLabel(QStringLiteral("Nur noetig, wenn du statt dessen direkt das HUD-Fenster aufnehmen willst:"));
    hint2->setObjectName(QStringLiteral("hint"));
    hint2->setWordWrap(true);
    lay->addWidget(hint2);

    m_cbFindable = new QCheckBox(QStringLiteral("HUD-Fenster fuer OBS auffindbar machen"));
    m_cbFindable->setChecked(m_state.obsFindable);
    m_cbFindable->setToolTip(QStringLiteral(
        "Das HUD laeuft normalerweise als Werkzeugfenster (WS_EX_TOOLWINDOW), und\n"
        "genau diesen Stil wirft OBS aus seiner Fensterliste. Der Schalter nimmt\n"
        "den Stil weg. Preis: ein Eintrag in Taskleiste und Alt-Tab."));
    connect(m_cbFindable, &QCheckBox::toggled, &m_hud, &hud::HudWindow::setFindable);
    lay->addWidget(m_cbFindable);

    m_cbChroma = new QCheckBox(QStringLiteral("HUD-Fenster einfarbig statt durchsichtig"));
    m_cbChroma->setChecked(m_state.obsChroma);
    m_cbChroma->setToolTip(QStringLiteral(
        "Faerbt den Fensterhintergrund einfarbig und macht ALLE Panels deckend -\n"
        "sonst frisst der Farbschluessel in OBS die halbtransparenten Panels an."));
    connect(m_cbChroma, &QCheckBox::toggled, this, &SubsystemsPanel::setChroma);
    lay->addWidget(m_cbChroma);

    auto* crow = new QHBoxLayout();
    crow->addWidget(new QLabel(QStringLiteral("Schluesselfarbe")));
    m_cmbChromaColor = new QComboBox();
    m_cmbChromaColor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cmbChromaColor->setMinimumContentsLength(12);
    // Magenta zuerst: Gruen kollidiert mit den Sektorfarben, Blau mit Alpine/Williams/RB.
    m_cmbChromaColor->addItem(QStringLiteral("Magenta (empfohlen)"), QStringLiteral("#FF00FF"));
    m_cmbChromaColor->addItem(QStringLiteral("Gruen"), QStringLiteral("#00FF00"));
    m_cmbChromaColor->addItem(QStringLiteral("Blau"), QStringLiteral("#0000FF"));
    const int cidx = m_cmbChromaColor->findData(m_state.obsChromaColor.toUpper());
    m_cmbChromaColor->setCurrentIndex(cidx >= 0 ? cidx : 0);
    connect(m_cmbChromaColor, &QComboBox::currentIndexChanged, this, &SubsystemsPanel::setChroma);
    crow->addWidget(m_cmbChromaColor, 1);
    lay->addLayout(crow);
    m_cmbChromaColor->setEnabled(m_state.obsChroma || m_state.obsWindow);
    return group;
}

void SubsystemsPanel::setChroma()
{
    const bool on = m_cbChroma->isChecked();
    const QString color = m_cmbChromaColor->currentData().toString();
    // Die Schluesselfarbe gilt fuer beide Wege - fuer das einfarbige HUD-Fenster
    // UND fuer das OBS-Fenster, das immer einfarbig ist.
    m_cmbChromaColor->setEnabled(on || m_cbObsWindow->isChecked());
    m_hud.setChroma(on, color);
    if (hud::ObsWindow* obs = m_obs(false))
        obs->setChromaColor(color);
}

void SubsystemsPanel::setObsWindow(bool on)
{
    // Es wird erst beim Einschalten gebaut - wer es nicht braucht, zahlt auch
    // nicht dafuer (es rendert die Szene ein zweites Mal).
    m_state.obsWindow = on;
    setChecked(m_cbObsWindow, on);
    m_cmbChromaColor->setEnabled(on || m_cbChroma->isChecked());
    if (on) {
        hud::ObsWindow* obs = m_obs(true);
        obs->setCanvas(m_spObsW->value(), m_spObsH->value());
        obs->setChromaColor(m_cmbChromaColor->currentData().toString());
        obs->show();
    } else if (hud::ObsWindow* obs = m_obs(false)) {
        obs->hide();
    }
}

void SubsystemsPanel::onObsCanvas()
{
    if (m_loading)
        return;
    m_state.obsW = m_spObsW->value();
    m_state.obsH = m_spObsH->value();
    if (hud::ObsWindow* obs = m_obs(false))
        obs->setCanvas(m_spObsW->value(), m_spObsH->value());
}

QSpinBox* SubsystemsPanel::spin(int lo, int hi, int value)
{
    auto* sp = new QSpinBox();
    sp->setRange(lo, hi);
    sp->setValue(value);
    sp->setMinimumWidth(64);
    sp->setMaximumWidth(110);
    return sp;
}

// ── Fuss: hier geht es zu den echten Seiten ──────────────────────────────────
QWidget* SubsystemsPanel::buildFooter()
{
    auto* wrap = new QWidget();
    auto* box = new QVBoxLayout(wrap);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);

    auto* row = new QHBoxLayout();
    auto* btnRegie = new QPushButton(QStringLiteral("Regie"));
    btnRegie->setToolTip(QStringLiteral("Manuelle Einblendungen: Charts, Strategie, WM-Stand"));
    connect(btnRegie, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(m_server.baseUrl() + QStringLiteral("/regie"))); });
    auto* btnSettings = new QPushButton(QStringLiteral("Settings"));
    btnSettings->setToolTip(QStringLiteral("Bausteine an/aus, Regler, Branding, Presets, Trackmap"));
    connect(btnSettings, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl(m_server.baseUrl() + QStringLiteral("/settings"))); });
    auto* btnUpd = new QPushButton(QStringLiteral("Nach Update suchen"));
    btnUpd->setToolTip(QStringLiteral("Fragt bei GitHub nach der neuesten Fassung.\n"
                                      "Beim Programmstart passiert das ohnehin einmal von selbst."));
    connect(btnUpd, &QPushButton::clicked, this, &SubsystemsPanel::updateSuchen);
    auto* btnLog = new QPushButton(QStringLiteral("Log"));
    btnLog->setToolTip(QStringLiteral("Meldungen von Server und HUD (stehen auch in data\\kers.log)"));
    connect(btnLog, &QPushButton::clicked, this, &SubsystemsPanel::logZeigen);
    row->addWidget(btnRegie);
    row->addWidget(btnSettings);
    row->addWidget(btnUpd);
    row->addWidget(btnLog);
    box->addLayout(row);

    // ── Vorherige Versionen ──────────────────────────────────────────────────
    // Auswaehlen ODER tippen. Der Wechsel laeuft durch dieselbe gepruefte Kette
    // wie ein Update (Groesse, SHA256, Austausch mit .bak).
    auto* ver = new QHBoxLayout();
    ver->setSpacing(6);
    ver->addWidget(new QLabel(QStringLiteral("Version:")));
    m_cmbFassung = new QComboBox();
    m_cmbFassung->setEditable(true);
    m_cmbFassung->setInsertPolicy(QComboBox::NoInsert);
    m_cmbFassung->lineEdit()->setPlaceholderText(QStringLiteral("wird geladen ..."));
    m_cmbFassung->setToolTip(QStringLiteral("Vorherige Versionen laden.\n"
                                            "Ins Feld klicken zeigt die Liste, Tippen sucht darin (z.B. 0.1.1)."));
    // Den EINGEBAUTEN Completer scharfstellen - ein eigener haette kein Modell
    // und faende nie etwas. MatchContains, damit auch "08-13" etwas findet.
    if (QCompleter* c = m_cmbFassung->completer()) {
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        c->setCompletionMode(QCompleter::PopupCompletion);
    }
    // Klick ins Feld zeigt die Liste - der Klick landet im QLineEdit, nicht in der ComboBox.
    m_cmbFassung->lineEdit()->installEventFilter(this);
    m_btnFassung = new QPushButton(QStringLiteral("Wechseln"));
    m_btnFassung->setEnabled(false);
    connect(m_btnFassung, &QPushButton::clicked, this, &SubsystemsPanel::fassungWechseln);
    connect(m_cmbFassung->lineEdit(), &QLineEdit::returnPressed, this, &SubsystemsPanel::fassungWechseln);
    ver->addWidget(m_cmbFassung, 1);
    ver->addWidget(m_btnFassung);
    box->addLayout(ver);

    auto* quit = new QPushButton(QStringLiteral("ALLES BEENDEN"));
    quit->setStyleSheet(QString::fromLatin1(DANGER));
    quit->setToolTip(QStringLiteral("Server beenden, HUD schliessen und dieses Fenster beenden."));
    connect(quit, &QPushButton::clicked, this, &SubsystemsPanel::shutdownAll);
    box->addWidget(quit);

    auto* hint = new QLabel(QStringLiteral(
        "Bausteine und alle Einstellungen liegen in Regie und Settings. "
        "Fenster schliessen blendet nur aus - zum Beenden 'Alles beenden' oder das Tray-Icon."));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    box->addWidget(hint);
    return wrap;
}

// ── HUD-Reaktionen ───────────────────────────────────────────────────────────
void SubsystemsPanel::onGeometrySpin()
{
    if (m_loading)
        return;
    m_hud.applyGeometry(m_spX->value(), m_spY->value(), m_spW->value(), m_spH->value());
}

void SubsystemsPanel::applyGeometry(int x, int y, int w, int h)
{
    // HUD wurde bewegt -> Zahlen nachziehen (ohne Rueckkopplung).
    m_loading = true;
    const struct { QSpinBox* sp; int v; } vals[] = {{m_spX, x}, {m_spY, y}, {m_spW, w}, {m_spH, h}};
    for (const auto& e : vals) {
        e.sp->blockSignals(true);
        e.sp->setValue(e.v);
        e.sp->blockSignals(false);
    }
    m_loading = false;
}

void SubsystemsPanel::setOnTop(bool on)
{
    m_state.panelOnTop = on;
    const bool wasVisible = isVisible();
    Qt::WindowFlags f = windowFlags();
    if (on)
        f |= Qt::WindowStaysOnTopHint;
    else
        f &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags(f);
    if (wasVisible)
        show();
}

// ── Status vom Server ────────────────────────────────────────────────────────
void SubsystemsPanel::applyStatus(bool serverOk, bool udpOk, int drivers)
{
    m_dotServer->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(serverOk ? QStringLiteral("#3ddc84") : QStringLiteral("#e10600")));
    m_lblServer->setText(serverOk ? QStringLiteral("Server online") : QStringLiteral("Server offline"));
    m_dotUdp->setStyleSheet(QStringLiteral("background: %1; border-radius: 5px;").arg(udpOk ? QStringLiteral("#3ddc84") : QStringLiteral("#e10600")));
    m_lblUdp->setText(udpOk ? QStringLiteral("UDP aktiv - %1 Fahrer").arg(drivers) : QStringLiteral("UDP-Daten"));

    if (serverOk) {
        m_starting = false;
        m_startTimeout.stop();
        m_btnServer->setEnabled(false);
        m_btnServer->setText(QStringLiteral("Server Run"));
        m_btnStop->setEnabled(m_server.canStart());
        m_btnStop->setText(QStringLiteral("Stop Server"));
    } else {
        m_btnStop->setEnabled(false);
        if (!m_starting) {
            m_btnServer->setEnabled(m_server.canStart());
            m_btnServer->setText(QStringLiteral("Start Server"));
        }
    }
}

// ── Update ───────────────────────────────────────────────────────────────────
void SubsystemsPanel::updateSuchen()
{
    // Suche von Hand - anders als beim Start darf sie sich melden. Die
    // Stummschaltung aus einem frueheren Fassungswechsel ist damit aufgehoben.
    m_updateManuell = true;
    m_state.updateStumm = false;
    m_lblVersion->setToolTip(QStringLiteral("Suche laeuft ..."));
    m_updater.check();
    m_updater.releasesLaden();
}

void SubsystemsPanel::updateGefunden(const QString& version, qint64 groesse, const QString& datum)
{
    const double mb = groesse / (1024.0 * 1024.0);
    // Nach einem bewussten Wechsel nicht bei jedem Start dieselbe neuere anbieten
    if (m_state.updateStumm && !m_updateManuell) {
        m_lblVersion->setToolTip(QStringLiteral("Neuere Fassung %1 vom %2 liegt bereit (%3 MB).\n"
                                                "Ausgeblendet, weil du diese Fassung bewusst gewaehlt hast - "
                                                "'Nach Update suchen' zeigt sie wieder.")
                                     .arg(version, datum)
                                     .arg(mb, 0, 'f', 0));
        return;
    }
    m_updateManuell = false;
    m_lblVersion->setText(QStringLiteral(u"v%1 → %2").arg(paths::appVersion(), version));
    m_lblVersion->setStyleSheet(QStringLiteral("color:#e10600;font-weight:bold"));
    m_lblVersion->setToolTip(QStringLiteral("Neue Fassung %1 vom %2\n%3 MB").arg(version, datum).arg(mb, 0, 'f', 0));
    m_updateBereit = false;
    m_btnUpdate->setText(QStringLiteral("Auf %1 aktualisieren  (%2 MB)").arg(version).arg(mb, 0, 'f', 0));
    m_btnUpdate->setEnabled(true);
    m_btnUpdate->show();
}

void SubsystemsPanel::updateAktuell()
{
    m_lblVersion->setToolTip(QStringLiteral(u"Version dieser Anwendung — auf dem neuesten Stand"));
    if (m_updateManuell) {
        m_updateManuell = false;
        QMessageBox::information(this, QStringLiteral("Nach Update suchen"),
                                 QStringLiteral("Version %1 ist die neueste.").arg(paths::appVersion()));
    }
}

void SubsystemsPanel::updateFehler(const QString& text)
{
    // Beim stillen Start-Check kein Dialog; die Meldung steht im Tooltip.
    m_lblVersion->setToolTip(QStringLiteral("Update-Suche: %1").arg(text));
    // Ein abgebrochener Download darf die Auswahl nicht dauerhaft sperren.
    m_btnFassung->setEnabled(!m_updater.releases().isEmpty());
    if (!m_updateBereit && !m_updater.busy())
        m_btnUpdate->hide();
    if (m_updateManuell) {
        m_updateManuell = false;
        QMessageBox::information(this, QStringLiteral("Nach Update suchen"), text);
    }
}

void SubsystemsPanel::updateKnopf()
{
    if (m_updateBereit) {
        const QString fehler = m_updater.tauschStarten();
        if (!fehler.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Update"), fehler);
            return;
        }
        // Der Helfer wartet, bis dieser Prozess weg ist - also jetzt beenden.
        QApplication::quit();
        return;
    }
    m_btnUpdate->setEnabled(false);
    m_btnUpdate->setText(QStringLiteral("Wird geladen ..."));
    m_updater.download();
}

void SubsystemsPanel::updateFortschritt(qint64 geladen, qint64 gesamt)
{
    if (gesamt > 0)
        m_btnUpdate->setText(QStringLiteral("Wird geladen ... %1 %  (%2 von %3 MB)")
                                 .arg(geladen * 100 / gesamt)
                                 .arg(geladen / (1024.0 * 1024.0), 0, 'f', 0)
                                 .arg(gesamt / (1024.0 * 1024.0), 0, 'f', 0));
}

void SubsystemsPanel::updateBereit(const QString&)
{
    m_updateBereit = true;
    m_btnUpdate->setText(QStringLiteral(u"Neu starten und übernehmen"));
    m_btnUpdate->setEnabled(true);
    m_btnUpdate->show();
}

void SubsystemsPanel::releasesDa(const QList<ReleaseInfo>& liste)
{
    m_cmbFassung->clear();
    if (liste.isEmpty()) {
        m_cmbFassung->lineEdit()->setPlaceholderText(QStringLiteral("keine Fassungen gefunden"));
        return;
    }
    for (const ReleaseInfo& r : liste) {
        const QString hier = r.version == paths::appVersion() ? QStringLiteral("  (laeuft)") : QString();
        m_cmbFassung->addItem(QStringLiteral("%1   %2   %3 MB%4")
                                  .arg(r.version, r.published)
                                  .arg(r.size / (1024.0 * 1024.0), 0, 'f', 0)
                                  .arg(hier),
                              r.version);
    }
    m_cmbFassung->setCurrentIndex(-1);
    m_cmbFassung->lineEdit()->setPlaceholderText(QStringLiteral("Vorherige Versionen"));
    // Beim Fuellen wechselt das Modell - der Completer haengt danach evtl. am alten.
    if (QCompleter* c = m_cmbFassung->completer()) {
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        c->setCompletionMode(QCompleter::PopupCompletion);
    }
    m_btnFassung->setEnabled(true);
}

QString SubsystemsPanel::gewaehlteFassung() const
{
    // Bei Auswahl steht die ganze Zeile im Feld, die nackte Nummer liegt als
    // Nutzdatum daneben. Getippt wird dagegen nur die Nummer.
    const int i = m_cmbFassung->currentIndex();
    if (i >= 0 && m_cmbFassung->itemText(i) == m_cmbFassung->currentText())
        return m_cmbFassung->itemData(i).toString();
    const QString t = m_cmbFassung->currentText().trimmed();
    return t.isEmpty() ? QString() : t.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
}

void SubsystemsPanel::fassungWechseln()
{
    const QString version = gewaehlteFassung();
    if (version.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Andere Fassung"), QStringLiteral("Bitte eine Version waehlen oder eintippen."));
        return;
    }
    const QString fehler = m_updater.waehlen(version);
    if (!fehler.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Andere Fassung"), fehler);
        return;
    }
    if (vergleiche(alsZahlen(version), Updater::ersteMitDatenordner()) < 0) {
        const auto antwort = QMessageBox::question(
            this, QStringLiteral("Aeltere Fassung"),
            QStringLiteral("Version %1 ist aelter als 0.1.1 und kennt den Ordner 'data' noch nicht.\n\n"
                           "Sie sucht ihre Dateien neben der EXE, findet dort nichts und legt frische Vorgaben an - "
                           "WM-Stand und Einstellungen wirken dann verschwunden.\n\n"
                           "Verloren geht nichts: beim Zurueckwechseln auf eine neuere Fassung sind die echten Daten "
                           "wieder da.\n\nTrotzdem laden?")
                .arg(version));
        if (antwort != QMessageBox::Yes)
            return;
    }
    // Bewusste Wahl: die stille Suche soll jetzt nicht bei jedem Start die neueste anbieten.
    m_state.updateStumm = true;
    m_btnFassung->setEnabled(false);
    m_updateBereit = false;
    m_btnUpdate->setText(QStringLiteral("Version %1 wird geladen ...").arg(version));
    m_btnUpdate->setEnabled(false);
    m_btnUpdate->show();
    m_updater.download();
}

void SubsystemsPanel::serverFehler(const QString& text)
{
    const bool vonHand = m_starting;
    m_starting = false;
    m_startTimeout.stop();
    m_btnServer->setEnabled(true);
    m_btnServer->setText(QStringLiteral("Start Server (Port belegt?)"));
    const QString lang = text + QStringLiteral("\n\nLaeuft noch ein anderer KERS-Server, z.B. die alte Python-Fassung?");
    m_btnServer->setToolTip(lang);
    if (vonHand)
        QMessageBox::warning(this, QStringLiteral("Server startet nicht"), lang);
}

void SubsystemsPanel::logZeigen()
{
    if (!m_log)
        m_log = new LogView(this);
    m_log->show();
    m_log->raise();
    m_log->activateWindow();
}

bool SubsystemsPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (m_cmbFassung && obj == m_cmbFassung->lineEdit() && event->type() == QEvent::MouseButtonPress
        && m_cmbFassung->count() > 0) {
        // Verzoegert: waehrend der Klick noch verarbeitet wird, schloesse das
        // Textfeld die gerade geoeffnete Liste sofort wieder.
        QTimer::singleShot(0, this, [this] {
            if (QCompleter* c = m_cmbFassung->completer()) {
                c->setCompletionPrefix(m_cmbFassung->currentText());
                c->complete();
            }
        });
    }
    return QWidget::eventFilter(obj, event);
}

// ── Fenster ──────────────────────────────────────────────────────────────────
void SubsystemsPanel::moveEvent(QMoveEvent* e)
{
    QWidget::moveEvent(e);
    m_state.panelX = x();
    m_state.panelY = y();
}

void SubsystemsPanel::closeEvent(QCloseEvent* e)
{
    // Nur ausblenden - beendet wird ueber das Tray-Icon oder "Alles beenden".
    e->ignore();
    hide();
    m_state.panelVisible = false;
}

}  // namespace kers::app
