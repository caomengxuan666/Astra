// =============================================================
// File: include/astra/cluster/ClusterBus.hpp
// Purpose: Minimal Redis Cluster Bus–like binary framing (skeleton)
// Notes :
//   * This is a pragmatic skeleton that mirrors Redis cluster bus
//     header layout at a high level so we can handshake with
//     official nodes. It only includes fields that are strictly
//     needed for MEET/PING/PONG + basic gossip.
//   * For production, align strictly with Redis' cluster.h/cluster.c
//     and add signature checks, CRC, versions, extra fields.
// =============================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>


namespace Astra::cluster::bus {

    // ---- Byte order helpers (portable, no system headers required) ----
    static inline uint16_t be16(uint16_t x) { return (uint16_t) (((x & 0x00FFu) << 8) | ((x & 0xFF00u) >> 8)); }
    static inline uint32_t be32(uint32_t x) { return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24); }
    static inline uint64_t be64(uint64_t x) {
        return ((uint64_t) be32(uint32_t(x & 0xFFFFFFFFULL)) << 32) | (uint64_t) be32(uint32_t((x >> 32) & 0xFFFFFFFFULL));
    }

    // Convert host->network (big endian) and back
    static inline uint16_t h2n16(uint16_t x) { return be16(x); }
    static inline uint32_t h2n32(uint32_t x) { return be32(x); }
    static inline uint64_t h2n64(uint64_t x) { return be64(x); }
    static inline uint16_t n2h16(uint16_t x) { return be16(x); }
    static inline uint32_t n2h32(uint32_t x) { return be32(x); }
    static inline uint64_t n2h64(uint64_t x) { return be64(x); }

    // ---- Message types (subset) ----
    enum class MsgType : uint16_t {
        PING = 0,// value placeholders; align with real values if you interop with Redis
        PONG = 1,
        MEET = 2,
    };

    // ---- Node flags (subset) ----
    constexpr uint16_t NODE_MASTER = 1 << 0;
    constexpr uint16_t NODE_SLAVE = 1 << 1;
    constexpr uint16_t NODE_FAIL = 1 << 2; // hard fail
    constexpr uint16_t NODE_PFAIL = 1 << 3;// possible fail

    // ---- Fixed sizes (compatible with IPv6 textual form) ----
    constexpr size_t NODE_ID_LEN = 40;// hex id
    constexpr size_t IP_STR_LEN = 46; // enough for IPv6

#pragma pack(push, 1)
    struct BusHeader {
        char signature[4];    // "RCmb" (Redis Cluster message bus)
        uint32_t totlen;      // total length including header (BE)
        uint16_t ver;         // protocol version (BE). Keep 1 here.
        uint16_t type;        // MsgType (BE)
        uint64_t currentEpoch;// (BE)
        uint64_t configEpoch; // (BE)
        char senderId[NODE_ID_LEN];
        char myip[IP_STR_LEN];
        uint16_t port; // client port (BE)
        uint16_t cport;// cluster bus port (BE)
        uint16_t flags;// NODE_* (BE)
        uint16_t count;// gossip entries count (BE)
        // NOTE: real Redis has more fields (link state, state hash, slots, etc.)
    };

    struct GossipEntry {
        char nodeId[NODE_ID_LEN];
        char ip[IP_STR_LEN];
        uint16_t port; // client port (BE)
        uint16_t cport;// cluster bus port (BE)
        uint16_t flags;// NODE_* (BE)
    };
#pragma pack(pop)

    // ---- Builders ----
    struct Wire {
        std::vector<uint8_t> buf;

        void write(const void *p, size_t n) {
            const auto *b = static_cast<const uint8_t *>(p);
            buf.insert(buf.end(), b, b + n);
        }
    };

    inline std::vector<uint8_t> buildFrame(
            MsgType type,
            const std::string &senderId,
            const std::string &myip,
            uint16_t clientPort,
            uint16_t clusterPort,
            uint16_t flags,
            uint64_t currentEpoch,
            uint64_t configEpoch,
            const std::vector<GossipEntry> &gossip) {
        BusHeader h{};
        std::memcpy(h.signature, "RCmb", 4);
        h.ver = h2n16(1);
        h.type = h2n16(static_cast<uint16_t>(type));
        h.currentEpoch = h2n64(currentEpoch);
        h.configEpoch = h2n64(configEpoch);
        std::memset(h.senderId, 0, NODE_ID_LEN);
        std::memcpy(h.senderId, senderId.c_str(), std::min(senderId.size(), NODE_ID_LEN));
        std::memset(h.myip, 0, IP_STR_LEN);
        std::memcpy(h.myip, myip.c_str(), std::min(myip.size(), IP_STR_LEN));
        h.port = h2n16(clientPort);
        h.cport = h2n16(clusterPort);
        h.flags = h2n16(flags);
        h.count = h2n16(static_cast<uint16_t>(gossip.size()));

        Wire w;
        size_t headPos = 0;
        w.write(&h, sizeof(h));
        for (const auto &g: gossip) {
            w.write(&g, sizeof(GossipEntry));
        }
        // Patch totlen
        uint32_t tot = static_cast<uint32_t>(w.buf.size());
        tot = h2n32(tot);
        std::memcpy(w.buf.data() + offsetof(BusHeader, totlen), &tot, sizeof(uint32_t));
        return std::move(w.buf);
    }

    // ---- Parser result ----
    struct Parsed {
        MsgType type{};
        std::string senderId;
        std::string myip;
        uint16_t port{};
        uint16_t cport{};
        uint16_t flags{};
        uint64_t currentEpoch{};
        uint64_t configEpoch{};
        std::vector<GossipEntry> gossip;
    };

    inline Parsed parseFrame(const uint8_t *data, size_t n) {
        if (n < sizeof(BusHeader)) throw std::runtime_error("short header");
        Parsed out{};
        BusHeader h{};
        std::memcpy(&h, data, sizeof(BusHeader));
        if (std::memcmp(h.signature, "RCmb", 4) != 0) throw std::runtime_error("bad signature");
        const uint32_t tot = n2h32(h.totlen);
        if (tot != n) throw std::runtime_error("length mismatch");
        out.type = static_cast<MsgType>(n2h16(h.type));
        out.currentEpoch = n2h64(h.currentEpoch);
        out.configEpoch = n2h64(h.configEpoch);
        out.port = n2h16(h.port);
        out.cport = n2h16(h.cport);
        out.flags = n2h16(h.flags);
        out.senderId.assign(h.senderId, h.senderId + strnlen(h.senderId, NODE_ID_LEN));
        out.myip.assign(h.myip, h.myip + strnlen(h.myip, IP_STR_LEN));

        const uint16_t cnt = n2h16(h.count);
        size_t off = sizeof(BusHeader);
        out.gossip.reserve(cnt);
        for (uint16_t i = 0; i < cnt; i++) {
            if (off + sizeof(GossipEntry) > n) throw std::runtime_error("gossip overflow");
            GossipEntry ge{};
            std::memcpy(&ge, data + off, sizeof(GossipEntry));
            // convert endian in-place
            ge.port = n2h16(ge.port);
            ge.cport = n2h16(ge.cport);
            ge.flags = n2h16(ge.flags);
            out.gossip.push_back(ge);
            off += sizeof(GossipEntry);
        }
        return out;
    }

    inline GossipEntry makeGossip(const std::string &id, const std::string &ip, uint16_t cport, uint16_t busPort, uint16_t flags) {
        GossipEntry g{};
        std::memset(g.nodeId, 0, NODE_ID_LEN);
        std::memcpy(g.nodeId, id.c_str(), std::min(id.size(), NODE_ID_LEN));
        std::memset(g.ip, 0, IP_STR_LEN);
        std::memcpy(g.ip, ip.c_str(), std::min(ip.size(), IP_STR_LEN));
        g.port = h2n16(cport);
        g.cport = h2n16(busPort);
        g.flags = h2n16(flags);
        return g;
    }

}// namespace Astra::cluster::bus
