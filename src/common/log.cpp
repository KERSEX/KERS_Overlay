#include "log.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace kers::log {

namespace {

constexpr int kKeepLines = 2000;

QMutex g_mutex;
QFile* g_file = nullptr;
QStringList g_ring;
QtMessageHandler g_previous = nullptr;

void writeLine(const QString& text)
{
    QMutexLocker lock(&g_mutex);
    g_ring.append(text);
    while (g_ring.size() > kKeepLines)
        g_ring.removeFirst();
    if (g_file && g_file->isOpen()) {
        g_file->write(text.toUtf8());
        g_file->write("\n");
        g_file->flush();
    }
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>((text + QLatin1Char('\n')).utf16()));
#endif
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const char* kind = "INFO";
    switch (type) {
    case QtDebugMsg:    kind = "DEBUG"; break;
    case QtInfoMsg:     kind = "INFO"; break;
    case QtWarningMsg:  kind = "WARN"; break;
    case QtCriticalMsg: kind = "FEHLER"; break;
    case QtFatalMsg:    kind = "FATAL"; break;
    }
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString text = QStringLiteral("%1 %2 %3").arg(stamp, QLatin1String(kind), msg);
    // QML-Meldungen tragen Datei und Zeile - die will man sehen.
    if (ctx.file && *ctx.file && QString::fromUtf8(ctx.file).contains(".qml"))
        text += QStringLiteral("  [%1:%2]").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
    writeLine(text);
    emit Sink::instance()->line(text);
    if (type == QtFatalMsg)
        abort();
}

}  // namespace

Sink* Sink::instance()
{
    static Sink* s = new Sink();
    return s;
}

QStringList Sink::recent() const
{
    QMutexLocker lock(&g_mutex);
    return g_ring;
}

void install(const QString& logFilePath)
{
    {
        QMutexLocker lock(&g_mutex);
        if (!g_file) {
            g_file = new QFile(logFilePath);
            if (!g_file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                delete g_file;
                g_file = nullptr;
            }
        }
    }
    g_previous = qInstallMessageHandler(handler);
}

void append(const QString& text)
{
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    const QString line = stamp + QLatin1Char(' ') + text;
    writeLine(line);
    emit Sink::instance()->line(line);
}

}  // namespace kers::log
