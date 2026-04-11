#ifndef AGC_SERVER_LOGIC_HPP
#define AGC_SERVER_LOGIC_HPP

#include "common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace agc::server_logic
{
    bool validateActivePacket(const Packet& packet);

    std::uint32_t computeTotalChunks(std::size_t totalSize,
        std::size_t chunkSize);

    std::string buildTransferStartText(const std::string& fileName,
        std::size_t totalSize,
        std::uint32_t totalChunks,
        std::uint32_t checksum);

    bool shouldSendCommand(std::uint32_t telemetryCount);
    bool shouldRequestAdditionalStatus(std::uint32_t telemetryCount);
    bool shouldStartLargeTransfer(std::uint32_t telemetryCount,
        bool transferTriggered);
}

#endif