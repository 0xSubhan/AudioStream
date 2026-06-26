#pragma once

#include <cstdint>

// htons / htonl — cross-platform byte-order helpers
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif


namespace audiostream {

#pragma pack(push, 1)
/**
 * RFC 3550 Real-time Transport Protocol (RTP) Header
 * Total size: 12 bytes
 */
struct RtpHeader {
    uint8_t flags;        // Version (2 bits), Padding (1 bit), Extension (1 bit), CSRC count (4 bits)
    uint8_t payloadType;  // Marker (1 bit), Payload type (7 bits)
    uint16_t sequenceNumber;
    uint32_t timestamp;
    uint32_t ssrc;
};
#pragma pack(pop)

/**
 * Packs the fields into the standard RTP header structure in network byte order.
 */
inline void packRtpHeader(RtpHeader& header, uint16_t seq, uint32_t ts, uint32_t ssrc, uint8_t pt = 96) {
    header.flags = (2 << 6);          // Version = 2, Padding/Ext/CSRC = 0
    header.payloadType = pt & 0x7F;   // Marker = 0, Payload Type = 96 (Opus dynamic format)
    header.sequenceNumber = htons(seq);
    header.timestamp = htonl(ts);
    header.ssrc = htonl(ssrc);
}

} // namespace audiostream
