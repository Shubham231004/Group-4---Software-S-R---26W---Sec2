#ifndef AGC_CLIENT_LOGIC_HPP
#define AGC_CLIENT_LOGIC_HPP

#include "common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace agc::client_logic
{
    TelemetryData generateTelemetry(std::uint32_t sampleIndex);

    bool parseTransferMetadata(const std::string& metadataText,
        std::size_t& expectedSize,
        std::uint32_t& expectedChunks,
        std::uint32_t& expectedChecksum);

    std::string buildTransferStatusText(bool transferOk,
        std::size_t actualSize,
        std::uint32_t actualChecksum);

    bool isTelemetryWithinExpectedOperationalRange(const TelemetryData& telemetry);
}

#endif