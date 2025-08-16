#pragma once
#include <cstdint>
#include <string>

namespace Astra::utils {

    class CRC16 {
    public:
        static uint16_t crc16(const char *buf, size_t len);
        static uint16_t getKeyHashSlot(const std::string &key);

    private:
        static const uint16_t crc16tab[256];
    };

}// namespace Astra::utils