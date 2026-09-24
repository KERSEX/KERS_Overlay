// Rohes Lesen aus einem UDP-Datagramm - Little-Endian-Leser mit Laengenpruefung.
//
// main.py liest die Pakete mit struct.unpack_from an Hand-Offsets. Hier genauso:
// keine gepackten Structs, sondern Leser je Feld. Jede Lesung prueft die Laenge,
// ein abgeschnittenes Paket liefert 0 statt eines Absturzes.
#pragma once

#include <QByteArray>

#include <cstdint>
#include <cstring>

namespace kers::server {

struct PacketHeader {
    uint16_t format = 0;         // erwartet 2026
    uint8_t packetId = 0;
    uint64_t sessionUid = 0;
    uint8_t playerIndex = 0;
    bool ok = false;
};

inline uint8_t u8(const QByteArray& d, int off)
{
    return (off >= 0 && off < d.size()) ? static_cast<uint8_t>(d.at(off)) : 0;
}
inline int8_t i8(const QByteArray& d, int off)
{
    return static_cast<int8_t>(u8(d, off));
}
inline uint16_t u16(const QByteArray& d, int off)
{
    if (off < 0 || off + 2 > d.size())
        return 0;
    uint16_t v;
    std::memcpy(&v, d.constData() + off, 2);
    return v;
}
inline uint32_t u32(const QByteArray& d, int off)
{
    if (off < 0 || off + 4 > d.size())
        return 0;
    uint32_t v;
    std::memcpy(&v, d.constData() + off, 4);
    return v;
}
inline uint64_t u64(const QByteArray& d, int off)
{
    if (off < 0 || off + 8 > d.size())
        return 0;
    uint64_t v;
    std::memcpy(&v, d.constData() + off, 8);
    return v;
}
inline float f32(const QByteArray& d, int off)
{
    if (off < 0 || off + 4 > d.size())
        return 0.0f;
    float v;
    std::memcpy(&v, d.constData() + off, 4);
    return v;
}
inline double f64(const QByteArray& d, int off)
{
    if (off < 0 || off + 8 > d.size())
        return 0.0;
    double v;
    std::memcpy(&v, d.constData() + off, 8);
    return v;
}

/// Header '<HBBBBBQfIIBB' (29 Bytes). Genutzt werden format, packetId, sessionUid, playerIndex.
PacketHeader parseHeader(const QByteArray& d);

}  // namespace kers::server
