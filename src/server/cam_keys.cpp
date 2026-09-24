#include "cam_keys.h"

#include <QDebug>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace kers::server {

namespace {

#ifdef Q_OS_WIN
bool sendInput(quint16 scan, bool extended, bool keyUp)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (keyUp ? KEYEVENTF_KEYUP : 0);
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}
#else
bool sendInput(quint16, bool, bool)
{
    return false;
}
#endif

}  // namespace

CamKeys::CamKeys(QObject* parent)
    : QObject(parent)
    , m_send(&sendInput)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CamKeys::run);
}

QStringList CamKeys::folgeFuer(int pos, const QString& weiter)
{
    if (pos >= 1 && pos <= 10)
        return {QString::number(pos % 10)};                          // 1..9, und 10 -> "0"
    if (pos >= 11 && pos <= 20)
        return {QStringLiteral("shift+") + QString::number((pos - 10) % 10)};
    if (pos >= 21 && pos <= 22 && !weiter.isEmpty()) {
        QStringList f{QStringLiteral("shift+0")};
        for (int i = 20; i < pos; ++i)
            f << weiter;
        return f;
    }
    return {};
}

bool CamKeys::scancode(const QString& taste, quint16& scan, bool& extended)
{
#ifdef Q_OS_WIN
    UINT vk = 0;
    extended = false;
    const QString t = taste.toLower();
    if (t.size() == 1 && (t.at(0).isDigit() || (t.at(0) >= QLatin1Char('a') && t.at(0) <= QLatin1Char('z')))) {
        vk = t.at(0).toUpper().unicode();                            // VK_0..VK_9, VK_A..VK_Z
    } else if (t.size() >= 2 && t.at(0) == QLatin1Char('f')) {
        bool ok = false;
        const int n = t.mid(1).toInt(&ok);
        if (!ok || n < 1 || n > 12)
            return false;
        vk = VK_F1 + (n - 1);
    } else {
        struct Named { const char* name; UINT vk; bool ext; };
        static const Named named[] = {
            {"shift", VK_LSHIFT, false}, {"ctrl", VK_LCONTROL, false}, {"alt", VK_LMENU, false},
            {"left", VK_LEFT, true}, {"right", VK_RIGHT, true}, {"up", VK_UP, true}, {"down", VK_DOWN, true},
            {"tab", VK_TAB, false}, {"space", VK_SPACE, false}, {"enter", VK_RETURN, false},
            {"pageup", VK_PRIOR, true}, {"pagedown", VK_NEXT, true}, {"home", VK_HOME, true}, {"end", VK_END, true},
            {"insert", VK_INSERT, true}, {"delete", VK_DELETE, true},
            {"add", VK_ADD, false}, {"subtract", VK_SUBTRACT, false}, {"multiply", VK_MULTIPLY, false},
            {"divide", VK_DIVIDE, true}, {"period", VK_OEM_PERIOD, false},
        };
        for (const Named& n : named) {
            if (t == QLatin1String(n.name)) {
                vk = n.vk;
                extended = n.ext;
                break;
            }
        }
        if (!vk)
            return false;
    }
    scan = static_cast<quint16>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    return scan != 0;
#else
    Q_UNUSED(taste);
    scan = 0;
    extended = false;
    return false;
#endif
}

QString CamKeys::druecke(const QStringList& folge, int haltenMs)
{
    const int halten = std::clamp(haltenMs > 0 ? haltenMs : 60, 10, 300);
    QList<Step> neu;
    for (int i = 0; i < folge.size(); ++i) {
        const QStringList teile = folge.at(i).split(QLatin1Char('+'));
        QList<Step> mods;
        for (int k = 0; k < teile.size() - 1; ++k) {
            Step s;
            if (!scancode(teile.at(k), s.scan, s.ext))
                return QStringLiteral("Unbekannte Taste: %1").arg(teile.at(k));
            mods << s;
        }
        Step taste;
        if (!scancode(teile.last(), taste.scan, taste.ext))
            return QStringLiteral("Unbekannte Taste: %1").arg(teile.last());

        if (i > 0 || !m_steps.isEmpty())
            neu << Step{halten};                                     // Abstand zwischen zwei Tasten
        for (const Step& m : mods)
            neu << m;
        if (!mods.isEmpty())
            neu << Step{20};                                         // Modifier liegt an, BEVOR die Taste kommt
        neu << taste;
        neu << Step{halten};
        Step los = taste;
        los.up = true;
        neu << los;
        for (int k = mods.size() - 1; k >= 0; --k) {
            Step m = mods.at(k);
            m.up = true;
            neu << m;
        }
    }
    const bool lief = busy();
    m_steps += neu;
    if (!lief)
        run();
    return QString();
}

void CamKeys::run()
{
    while (!m_steps.isEmpty()) {
        const Step s = m_steps.takeFirst();
        if (s.waitMs > 0) {
            m_timer.start(s.waitMs);
            return;
        }
        if (!m_send(s.scan, s.ext, s.up))
            qWarning() << "[CAM] Tastendruck fehlgeschlagen, Scancode" << Qt::hex << s.scan;
    }
}

}  // namespace kers::server
