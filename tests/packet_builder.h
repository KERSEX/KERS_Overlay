// Synthetische F1-26-Pakete fuer die Tests - wie make_*() in Test\test.py.
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <cstring>

namespace kers::test {

constexpr int HDR = 29;

class Pkt {
public:
    /// Leeres Paket der Gesamtlaenge `size` mit gueltigem Header (Format 2026).
    Pkt(int packetId, int size, uint64_t sessionUid = 42, int playerIdx = 0)
        : m_d(size, '\0')
    {
        u16(0, 2026);
        u8(2, 26);
        u8(6, packetId);
        u64(7, sessionUid);
        u8(27, playerIdx);
        u8(28, 255);
    }

    Pkt& u8(int off, int v) { m_d[off] = static_cast<char>(static_cast<uint8_t>(v)); return *this; }
    Pkt& i8(int off, int v) { return u8(off, static_cast<uint8_t>(static_cast<int8_t>(v))); }
    Pkt& u16(int off, int v) { const uint16_t x = static_cast<uint16_t>(v); std::memcpy(m_d.data() + off, &x, 2); return *this; }
    Pkt& u32(int off, uint32_t v) { std::memcpy(m_d.data() + off, &v, 4); return *this; }
    Pkt& u64(int off, uint64_t v) { std::memcpy(m_d.data() + off, &v, 8); return *this; }
    Pkt& f32(int off, float v) { std::memcpy(m_d.data() + off, &v, 4); return *this; }
    Pkt& f64(int off, double v) { std::memcpy(m_d.data() + off, &v, 8); return *this; }
    Pkt& str(int off, const QByteArray& s) { std::memcpy(m_d.data() + off, s.constData(), s.size()); return *this; }
    Pkt& fill(int off, int len, int v) { std::memset(m_d.data() + off, v, len); return *this; }

    const QByteArray& data() const { return m_d; }
    operator const QByteArray&() const { return m_d; }

private:
    QByteArray m_d;
};

// Paketgroessen, soweit der Server sie liest
constexpr int SIZE_MOTION = HDR + 24 * 54;
constexpr int SIZE_SESSION_MIN = HDR + 127;
constexpr int SIZE_LAPDATA = HDR + 24 * 57 + 2;
constexpr int SIZE_PARTICIPANTS = HDR + 1 + 24 * 60;
constexpr int SIZE_TELEMETRY = HDR + 24 * 59 + 3;
constexpr int SIZE_STATUS = HDR + 24 * 59;
constexpr int SIZE_FINAL = HDR + 1 + 24 * 46;
constexpr int SIZE_DAMAGE = HDR + 24 * 46;
constexpr int SIZE_HISTORY = HDR + 7 + 100 * 14;
constexpr int SIZE_LAPPOS = HDR + 2 + 50 * 24;
constexpr int SIZE_TELEM2 = HDR + 24 * 10;
constexpr int SIZE_EVENT = HDR + 4 + 12;

/// Event-Paket mit 4-Zeichen-Code und Detailbytes ab Offset HDR+4.
inline Pkt event(const char* code, const QByteArray& details = {}, uint64_t uid = 42)
{
    Pkt p(3, SIZE_EVENT, uid);
    p.str(HDR, QByteArray(code, 4));
    if (!details.isEmpty())
        p.str(HDR + 4, details);
    return p;
}

}  // namespace kers::test
