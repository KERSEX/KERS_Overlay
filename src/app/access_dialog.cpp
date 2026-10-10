#include "access_dialog.h"

#include "access_guard.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkInterface>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace kers::app {

namespace {

QLabel* ueberschrift(const QString& text)
{
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color: #e10600; font-weight: bold; letter-spacing: 1px; margin-top: 8px;"));
    return l;
}

QLabel* hinweis(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("hint"));
    l->setWordWrap(true);
    return l;
}

QTreeWidget* tabelle(const QStringList& spalten)
{
    auto* t = new QTreeWidget();
    t->setHeaderLabels(spalten);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(true);
    t->setMinimumHeight(110);
    return t;
}

}  // namespace

AccessDialog::AccessDialog(server::AccessGuard& guard, quint16 httpPort, QWidget* parent)
    : QDialog(parent)
    , m_guard(guard)
    , m_port(httpPort)
{
    setWindowTitle(QStringLiteral("KERS – Zugriff anderer Geräte"));
    resize(560, 640);
    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(6);

    // Code + Schalter
    auto* kopf = new QHBoxLayout();
    kopf->addWidget(new QLabel(QStringLiteral("Code:")));
    m_code = new QLabel();
    m_code->setObjectName(QStringLiteral("code"));
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    kopf->addWidget(m_code, 1);
    auto* btnNeu = new QPushButton(QStringLiteral("Neuer Code"));
    btnNeu->setToolTip(QStringLiteral("Freigegebene Geräte bleiben freigegeben - der neue Code gilt nur für neue."));
    connect(btnNeu, &QPushButton::clicked, this, [this] { m_guard.newCode(); });
    kopf->addWidget(btnNeu);
    lay->addLayout(kopf);
    m_enabled = new QCheckBox(QStringLiteral("Code für andere Geräte verlangen"));
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool on) { m_guard.setEnabled(on); });
    lay->addWidget(m_enabled);
    lay->addWidget(hinweis(QStringLiteral(
        "Dieser PC braucht nie einen Code (OBS, HUD, Regie im Browser hier). Handy, Tablet oder ein "
        "zweiter PC geben ihn einmal ein und bleiben dann freigegeben. Gesperrte Geräte und IPs "
        "bleiben gesperrt – auch nach einem Neustart und auch, wenn der Code aus ist.")));

    // Freigegebene Geraete
    lay->addWidget(ueberschrift(QStringLiteral("FREIGEGEBENE GERÄTE")));
    m_devices = tabelle({QStringLiteral("Name"), QStringLiteral("IP"), QStringLiteral("Zuletzt"), QStringLiteral("Status")});
    lay->addWidget(m_devices, 2);
    auto* zeile1 = new QHBoxLayout();
    m_btnSperren = new QPushButton(QStringLiteral("Sperren"));
    connect(m_btnSperren, &QPushButton::clicked, this, [this] {
        const QString id = gewaehltesGeraet();
        if (!id.isEmpty())
            m_guard.setDeviceBlocked(id, !m_guard.isDeviceBlocked(id));
    });
    m_btnEntfernen = new QPushButton(QStringLiteral("Entfernen"));
    m_btnEntfernen->setToolTip(QStringLiteral("Das Gerät muss danach den Code neu eingeben."));
    connect(m_btnEntfernen, &QPushButton::clicked, this, [this] {
        const QString id = gewaehltesGeraet();
        if (!id.isEmpty())
            m_guard.removeDevice(id);
    });
    auto* btnLink = new QPushButton(QStringLiteral("Link für OBS / zweiten PC …"));
    btnLink->setToolTip(QStringLiteral("Legt ein Gerät an und gibt eine Adresse mit Schlüssel aus - "
                                       "für eine OBS-Browserquelle, die keinen Code eintippen kann."));
    connect(btnLink, &QPushButton::clicked, this, &AccessDialog::linkErstellen);
    zeile1->addWidget(m_btnSperren);
    zeile1->addWidget(m_btnEntfernen);
    zeile1->addStretch(1);
    zeile1->addWidget(btnLink);
    lay->addLayout(zeile1);
    connect(m_devices, &QTreeWidget::itemSelectionChanged, this, &AccessDialog::refresh);

    // Versuche ohne Code
    lay->addWidget(ueberschrift(QStringLiteral("VERSUCHE OHNE CODE")));
    m_attempts = tabelle({QStringLiteral("IP"), QStringLiteral("Gerät"), QStringLiteral("Anzahl"), QStringLiteral("Zuletzt")});
    lay->addWidget(m_attempts, 1);
    auto* zeile2 = new QHBoxLayout();
    m_btnIpSperren = new QPushButton(QStringLiteral("IP sperren"));
    connect(m_btnIpSperren, &QPushButton::clicked, this, [this] {
        if (auto* it = m_attempts->currentItem())
            m_guard.setIpBlocked(it->text(0), true);
    });
    auto* btnLeeren = new QPushButton(QStringLiteral("Liste leeren"));
    connect(btnLeeren, &QPushButton::clicked, this, [this] { m_guard.clearAttempts(); });
    zeile2->addWidget(m_btnIpSperren);
    zeile2->addWidget(btnLeeren);
    zeile2->addStretch(1);
    lay->addLayout(zeile2);
    connect(m_attempts, &QTreeWidget::itemSelectionChanged, this, &AccessDialog::refresh);

    // Gesperrte IPs
    lay->addWidget(ueberschrift(QStringLiteral("GESPERRTE IPS")));
    m_blocked = new QListWidget();
    m_blocked->setMaximumHeight(90);
    lay->addWidget(m_blocked);
    auto* zeile3 = new QHBoxLayout();
    m_btnEntsperren = new QPushButton(QStringLiteral("Entsperren"));
    connect(m_btnEntsperren, &QPushButton::clicked, this, [this] {
        if (auto* it = m_blocked->currentItem())
            m_guard.setIpBlocked(it->text(), false);
    });
    connect(m_blocked, &QListWidget::itemSelectionChanged, this, &AccessDialog::refresh);
    zeile3->addWidget(m_btnEntsperren);
    zeile3->addStretch(1);
    auto* zu = new QPushButton(QStringLiteral("Schließen"));
    connect(zu, &QPushButton::clicked, this, &QDialog::accept);
    zeile3->addWidget(zu);
    lay->addLayout(zeile3);

    connect(&m_guard, &server::AccessGuard::changed, this, &AccessDialog::refresh);
    refresh();
}

QString AccessDialog::zeit(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("–");
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    if (t.date() == QDate::currentDate())
        return QStringLiteral("heute ") + t.toString(QStringLiteral("HH:mm"));
    return t.toString(QStringLiteral("dd.MM. HH:mm"));
}

QString AccessDialog::gewaehltesGeraet() const
{
    const QTreeWidgetItem* it = m_devices->currentItem();
    return it && it->isSelected() ? it->data(0, Qt::UserRole).toString() : QString();
}

void AccessDialog::refresh()
{
    QString code = m_guard.code();
    code.insert(3, QLatin1Char(' '));
    m_code->setText(code);
    m_enabled->blockSignals(true);
    m_enabled->setChecked(m_guard.enabled());
    m_enabled->blockSignals(false);

    // Auswahl ueber das Neuzeichnen retten
    const QString warGeraet = gewaehltesGeraet();
    const QString warVersuch = m_attempts->currentItem() ? m_attempts->currentItem()->text(0) : QString();
    const QString warIp = m_blocked->currentItem() ? m_blocked->currentItem()->text() : QString();

    m_devices->blockSignals(true);
    m_devices->clear();
    for (const auto& d : m_guard.devices()) {
        auto* it = new QTreeWidgetItem({d.name, d.ip.isEmpty() ? QStringLiteral("–") : d.ip, zeit(d.lastSeen),
                                        d.blocked ? QStringLiteral("gesperrt") : QStringLiteral("frei")});
        it->setData(0, Qt::UserRole, d.id);
        if (d.blocked)
            it->setForeground(3, QColor(0xff, 0x5f, 0x5a));
        m_devices->addTopLevelItem(it);
        if (d.id == warGeraet)
            m_devices->setCurrentItem(it);
    }
    m_devices->blockSignals(false);

    m_attempts->blockSignals(true);
    m_attempts->clear();
    for (const auto& a : m_guard.attempts()) {
        auto* it = new QTreeWidgetItem({a.ip, server::AccessGuard::deviceNameFromAgent(a.agent),
                                        QString::number(a.count), zeit(a.last)});
        it->setToolTip(1, a.agent);
        if (m_guard.isIpBlocked(a.ip))
            it->setForeground(0, QColor(0xff, 0x5f, 0x5a));
        m_attempts->addTopLevelItem(it);
        if (a.ip == warVersuch)
            m_attempts->setCurrentItem(it);
    }
    m_attempts->blockSignals(false);

    m_blocked->blockSignals(true);
    m_blocked->clear();
    for (const QString& ip : m_guard.blockedIps()) {
        m_blocked->addItem(ip);
        if (ip == warIp)
            m_blocked->setCurrentRow(m_blocked->count() - 1);
    }
    m_blocked->blockSignals(false);

    for (int i = 0; i < 3; ++i) {
        m_devices->resizeColumnToContents(i);
        m_attempts->resizeColumnToContents(i);
    }
    const QString id = gewaehltesGeraet();
    m_btnSperren->setEnabled(!id.isEmpty());
    m_btnSperren->setText(!id.isEmpty() && m_guard.isDeviceBlocked(id) ? QStringLiteral("Freigeben")
                                                                       : QStringLiteral("Sperren"));
    m_btnEntfernen->setEnabled(!id.isEmpty());
    m_btnIpSperren->setEnabled(m_attempts->currentItem() && m_attempts->currentItem()->isSelected());
    m_btnEntsperren->setEnabled(m_blocked->currentItem() && m_blocked->currentItem()->isSelected());
}

QString AccessDialog::lanAddress()
{
    QString fallback;
    for (const QNetworkInterface& nif : QNetworkInterface::allInterfaces()) {
        const auto f = nif.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry& e : nif.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const QString s = a.toString();
            // Private Netze zuerst (Heimnetz), sonst irgendeine
            if (a.isPrivateUse())
                return s;
            if (fallback.isEmpty() && !a.isLinkLocal())
                fallback = s;
        }
    }
    return fallback.isEmpty() ? QStringLiteral("127.0.0.1") : fallback;
}

void AccessDialog::linkErstellen()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Link für OBS / zweiten PC"),
                                               QStringLiteral("Name des Geräts (steht dann in der Liste):"),
                                               QLineEdit::Normal, QStringLiteral("OBS zweiter PC"), &ok);
    if (!ok)
        return;
    const QByteArray key = m_guard.createDevice(name);
    const QString url = QStringLiteral("http://%1:%2/?key=%3").arg(lanAddress()).arg(m_port).arg(QString::fromLatin1(key));
    QApplication::clipboard()->setText(url);

    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Link erstellt"));
    auto* lay = new QVBoxLayout(&d);
    lay->addWidget(new QLabel(QStringLiteral("Adresse für die OBS-Browserquelle (schon in der Zwischenablage):")));
    auto* feld = new QLineEdit(url);
    feld->setReadOnly(true);
    feld->setMinimumWidth(480);
    feld->selectAll();
    lay->addWidget(feld);
    lay->addWidget(hinweis(QStringLiteral(
        "Für einzelne Bausteine den Pfad tauschen und ?key=… behalten, z. B. /part/tower?key=… oder "
        "/regie?key=…. Der Schlüssel ist ein Passwort – nicht öffentlich zeigen. Weg damit: in der "
        "Geräteliste entfernen.")));
    auto* knoepfe = new QDialogButtonBox(QDialogButtonBox::Ok);
    auto* kopieren = knoepfe->addButton(QStringLiteral("Kopieren"), QDialogButtonBox::ActionRole);
    connect(kopieren, &QPushButton::clicked, &d, [url] { QApplication::clipboard()->setText(url); });
    connect(knoepfe, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    lay->addWidget(knoepfe);
    d.exec();
}

}  // namespace kers::app
