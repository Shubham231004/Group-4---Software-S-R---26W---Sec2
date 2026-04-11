#include "client_logic.hpp"

#include <string>

namespace agc::client_logic
{
    namespace
    {
        bool extractField(const std::string& text,
            const std::string& key,
            std::string& value)
        {
            const std::size_t keyPos = text.find(key);
            if (keyPos == std::string::npos)
            {
                return false;
            }

            const std::size_t valueStart = keyPos + key.size();
            std::size_t valueEnd = text.find(';', valueStart);
            if (valueEnd == std::string::npos)
            {
                valueEnd = text.size();
            }

            if (valueStart >= valueEnd)
            {
                return false;
            }

            value = text.substr(valueStart, valueEnd - valueStart);
            return true;
        }
    }

    TelemetryData generateTelemetry(std::uint32_t sampleIndex)
    {
        TelemetryData telemetry{};
        telemetry.altitudeFt = 30000.0 + static_cast<double>(sampleIndex * 100U);
        telemetry.speedKnots = 450.0 + static_cast<double>(sampleIndex * 2U);
        telemetry.headingDeg = 90.0 + static_cast<double>(sampleIndex);
        telemetry.fuelPercent = 80.0 - static_cast<double>(sampleIndex);
        return telemetry;
    }

    bool parseTransferMetadata(const std::string& metadataText,
        std::size_t& expectedSize,
        std::uint32_t& expectedChunks,
        std::uint32_t& expectedChecksum)
    {
        std::string sizeText{};
        std::string chunksText{};
        std::string checksumText{};

        if (extractField(metadataText, "size=", sizeText) == false)
        {
            return false;
        }

        if (extractField(metadataText, "chunks=", chunksText) == false)
        {
            return false;
        }

        if (extractField(metadataText, "checksum=", checksumText) == false)
        {
            return false;
        }

        try
        {
            expectedSize = static_cast<std::size_t>(std::stoull(sizeText));
            expectedChunks = static_cast<std::uint32_t>(std::stoul(chunksText));
            expectedChecksum = static_cast<std::uint32_t>(std::stoul(checksumText));
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    std::string buildTransferStatusText(bool transferOk,
        std::size_t actualSize,
        std::uint32_t actualChecksum)
    {
        return std::string("TRANSFER_OK=") +
            (transferOk ? "TRUE" : "FALSE") +
            ";SIZE=" + std::to_string(actualSize) +
            ";CHECKSUM=" + std::to_string(actualChecksum);
    }

    bool isTelemetryWithinExpectedOperationalRange(const TelemetryData& telemetry)
    {
        return (telemetry.altitudeFt >= 0.0) &&
            (telemetry.altitudeFt <= 50000.0) &&
            (telemetry.speedKnots >= 0.0) &&
            (telemetry.speedKnots <= 1000.0) &&
            (telemetry.headingDeg >= 0.0) &&
            (telemetry.headingDeg <= 360.0) &&
            (telemetry.fuelPercent >= 0.0) &&
            (telemetry.fuelPercent <= 100.0);
    }
}