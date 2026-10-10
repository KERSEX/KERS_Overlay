// KERS Subsystems - das kleine Schaltbrett fuer den Desktop.
// Portierung von hud/subsystems_panel.py.
//
// Bewusst NUR das, was es sonst nirgends gibt: den Server starten/stoppen und
// das HUD-Fenster an/aus, auf den Bildschirm legen, sperren, platzieren, OBS.
// Alles, was Konfiguration ist, bleibt in /regie und /settings - beide sind von
// hier aus einen Knopfdruck entfernt. Nichts davon wird hier nachgebaut, es
// gaebe sonst zwei Wahrheiten fuer dieselbe Einstellung.
//
// Eigenes Fenster und nicht Teil des HUDs: sobald das HUD gesperrt ist, gehen
// Klicks durch es hindurch - darin liesse sich also nichts mehr bedienen.
#pragma once

#include "hud_state.h"
#include "hud_window.h"
#include "obs_window.h"
#include "server_control.h"
#include "updater.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

#include <functional>

namespace kers::app {

class SubsystemsPanel : public QWidget {
    Q_OBJECT
public:
    /// obsWindow(ensure): das Aufnahmefenster holen; ensure=true legt es bei Bedarf an.
    using ObsAccessor = std::function<hud::ObsWindow*(bool ensure)>;

    SubsystemsPanel(HudState& state, hud::HudWindow& hud, ServerControl& server, ObsAccessor obs);

    void fillScreen();
    void setObsWindow(bool on);
    void shutdownAll();
    void startServer();
    void stopServer();

    void logZeigen();

protected:
    void moveEvent(QMoveEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget* buildHeader();
    QWidget* buildPower();
    QWidget* buildHudGroup();
    QWidget* buildObsGroup();
    QWidget* buildAccessGroup();
    void refreshAccess();
    void remoteAnmelden();
    QWidget* buildFooter();
    QSpinBox* spin(int lo, int hi, int value);
    static QLabel* dot(const QString& color);

    void applyStatus(bool serverOk, bool udpOk, int drivers);
    void applyPower(bool on);
    void applyGeometry(int x, int y, int w, int h);
    void applyLayoutEdit(bool on);
    void beschrifteLayoutKnopf(bool on);
    void layoutEditUmschalten(bool on);
    void layoutZuruecksetzen();
    void centerHud();
    void onGeometrySpin();
    void onObsCanvas();
    void setChroma();
    void setOnTop(bool on);
    static void setChecked(QAbstractButton* b, bool v);

    // Update (Portierung der _update_*-Methoden)
    void updateSuchen();
    void updateGefunden(const QString& version, qint64 groesse, const QString& datum);
    void updateAktuell();
    void updateFehler(const QString& text);
    void updateKnopf();
    void updateFortschritt(qint64 geladen, qint64 gesamt);
    void updateBereit(const QString& pfad);
    void releasesDa(const QList<ReleaseInfo>& liste);
    QString gewaehlteFassung() const;
    void fassungWechseln();
    void serverFehler(const QString& text);

    HudState& m_state;
    hud::HudWindow& m_hud;
    ServerControl& m_server;
    ObsAccessor m_obs;
    bool m_loading = false;
    bool m_starting = false;
    QTimer m_startTimeout;

    QLabel* m_dotServer = nullptr;
    QLabel* m_lblServer = nullptr;
    QLabel* m_dotUdp = nullptr;
    QLabel* m_lblUdp = nullptr;
    QLabel* m_lblVersion = nullptr;
    QPushButton* m_btnUpdate = nullptr;
    QPushButton* m_btnServer = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnPower = nullptr;
    QPushButton* m_btnLayout = nullptr;
    QCheckBox* m_cbLocked = nullptr;
    QComboBox* m_cmbHz = nullptr;
    QSpinBox* m_spX = nullptr;
    QSpinBox* m_spY = nullptr;
    QSpinBox* m_spW = nullptr;
    QSpinBox* m_spH = nullptr;
    QCheckBox* m_cbOnTop = nullptr;
    QCheckBox* m_cbObsWindow = nullptr;
    QSpinBox* m_spObsW = nullptr;
    QSpinBox* m_spObsH = nullptr;
    QCheckBox* m_cbFindable = nullptr;
    QCheckBox* m_cbChroma = nullptr;
    QComboBox* m_cmbChromaColor = nullptr;
    QComboBox* m_cmbFassung = nullptr;
    // Zugriff (ab 0.3.1)
    QLabel* m_lblCode = nullptr;
    QCheckBox* m_cbCode = nullptr;
    QLabel* m_lblAccess = nullptr;
    class QLineEdit* m_remoteCode = nullptr;
    QLabel* m_lblRemote = nullptr;
    QPushButton* m_btnFassung = nullptr;

    Updater m_updater;
    bool m_updateManuell = false;   // Suche kam per Knopf und darf sich melden
    bool m_updateBereit = false;    // .new liegt bereit -> Knopf tauscht
    QWidget* m_log = nullptr;
};

}  // namespace kers::app
