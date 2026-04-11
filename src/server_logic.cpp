#include "server_logic.hpp"

namespace agc::server_logic
{
    bool validateActivePacket(const Packet& packet)
    {
        const bool idValid = agc::validateAircraftId(packet.header.aircraftId);
        const bool timeValid =
            agc::validateTimestamp(packet.header.timestampMs, agc::getCurrentTimeMs());
        const bool sizeValid = agc::validatePayloadSize(packet.header.payloadSize);

        return (idValid && timeValid && sizeValid);
    }

    std::uint32_t computeTotalChunks(std::size_t totalSize,
        std::size_t chunkSize)
    {
        if (chunkSize == 0U)
        {
            return 0U;
        }

        return static_cast<std::uint32_t>(
            (totalSize + chunkSize - 1U) / chunkSize);
    }

    std::string buildTransferStartText(const std::string& fileName,
        std::size_t totalSize,
        std::uint32_t totalChunks,
        std::uint32_t checksum)
    {
        return "filename=" + fileName +
            ";size=" + std::to_string(totalSize) +
            ";chunks=" + std::to_string(totalChunks) +
            ";checksum=" + std::to_string(checksum);
    }

    bool shouldSendCommand(std::uint32_t telemetryCount)
    {
        return (telemetryCount == 2U);
    }

    bool shouldRequestAdditionalStatus(std::uint32_t telemetryCount)
    {
        return (telemetryCount == 3U);
    }

    bool shouldStartLargeTransfer(std::uint32_t telemetryCount,
        bool transferTriggered)
    {
        return ((telemetryCount == 5U) && (transferTriggered == false));
    }
}