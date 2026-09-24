#include "packets.h"

#include "constants.h"

namespace kers::server {

PacketHeader parseHeader(const QByteArray& d)
{
    PacketHeader h;
    if (d.size() < HEADER_SIZE)
        return h;
    // <H  packetFormat        @0
    // <B  gameYear            @2
    // <B  gameMajorVersion    @3
    // <B  gameMinorVersion    @4
    // <B  packetVersion       @5
    // <B  packetId            @6
    // <Q  sessionUID          @7
    // <f  sessionTime         @15
    // <I  frameIdentifier     @19
    // <I  overallFrameId      @23
    // <B  playerCarIndex      @27
    // <B  secondaryPlayerIdx  @28
    h.format = u16(d, 0);
    h.packetId = u8(d, 6);
    h.sessionUid = u64(d, 7);
    h.playerIndex = u8(d, 27);
    h.ok = true;
    return h;
}

}  // namespace kers::server
