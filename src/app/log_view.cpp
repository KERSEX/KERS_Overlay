#include "log_view.h"

#include "icon.h"
#include "log.h"
#include "paths.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

namespace kers::app {

LogView::LogView(QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_text(new QPlainTextEdit(this))
{
    setWindowTitle(QStringLiteral("KERS Subsystems - Log"));
    setWindowIcon(makeIcon());
    resize(820, 420);
    setStyleSheet(QStringLiteral("QWidget { background: #14141b; color: #e8e8ee; }"
                                 "QPlainTextEdit { background: #0e0e13; border: 1px solid #2a2a35; }"
                                 "QPushButton { background: #22222c; border: 1px solid #33333f; border-radius: 5px; padding: 5px 10px; }"
                                 "QPushButton:hover { background: #2c2c38; }"));

    m_text->setReadOnly(true);
    m_text->setMaximumBlockCount(2000);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* ordner = new QPushButton(QStringLiteral("Datenordner oeffnen"));
    connect(ordner, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(paths::dataDir()));
    });
    auto* kopieren = new QPushButton(QStringLiteral("Alles kopieren"));
    connect(kopieren, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_text->toPlainText());
    });
    auto* leiste = new QHBoxLayout();
    leiste->addWidget(ordner);
    leiste->addWidget(kopieren);
    leiste->addStretch(1);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->addWidget(m_text, 1);
    lay->addLayout(leiste);

    for (const QString& z : log::Sink::instance()->recent())
        m_text->appendPlainText(z);
    connect(log::Sink::instance(), &log::Sink::line, this, &LogView::anhaengen);
}

void LogView::anhaengen(const QString& zeile)
{
    // Nur mitscrollen, wenn man ohnehin unten stand - sonst springt es beim Lesen weg
    QScrollBar* sb = m_text->verticalScrollBar();
    const bool unten = sb->value() >= sb->maximum() - 2;
    m_text->appendPlainText(zeile);
    if (unten)
        sb->setValue(sb->maximum());
}

}  // namespace kers::app
