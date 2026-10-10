// "Geraete verwalten" - der Zugriffsschutz im Detail (ab 0.3.1).
//
// Im Schaltbrett stehen nur Code und Schalter; hier die Listen: freigegebene
// Geraete (sperren, entfernen), fremde Versuche ohne Code (IP sperren), gesperrte
// IPs (entsperren) und der Link fuer eine OBS-Quelle auf einem zweiten PC.
#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;

namespace kers::server {
class AccessGuard;
}

namespace kers::app {

class AccessDialog : public QDialog {
    Q_OBJECT
public:
    AccessDialog(server::AccessGuard& guard, quint16 httpPort, QWidget* parent = nullptr);

    /// Erste LAN-Adresse dieses PCs (192.168.x.x & Co.), sonst 127.0.0.1 - fuer Links.
    static QString lanAddress();
    static QString zeit(qint64 ms);

private:
    void refresh();
    void linkErstellen();
    QString gewaehltesGeraet() const;

    server::AccessGuard& m_guard;
    quint16 m_port;
    QLabel* m_code = nullptr;
    QCheckBox* m_enabled = nullptr;
    QTreeWidget* m_devices = nullptr;
    QTreeWidget* m_attempts = nullptr;
    QListWidget* m_blocked = nullptr;
    QPushButton* m_btnSperren = nullptr;
    QPushButton* m_btnEntfernen = nullptr;
    QPushButton* m_btnIpSperren = nullptr;
    QPushButton* m_btnEntsperren = nullptr;
};

}  // namespace kers::app
