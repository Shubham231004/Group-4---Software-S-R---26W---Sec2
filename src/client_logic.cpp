#include "client_logic.hpp"

#include <string>

/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian

This file implements helper logic used specifically by the aircraft client.
These functions support the client’s higher-level data preparation and validation
without directly performing low-level socket communication.

Main requirements implemented here:
- generating simulated telemetry values,
- extracting and parsing metadata for large transfer,
- building transfer completion status text,
- validating telemetry values against expected operational ranges.

Importance of this file:
The client is responsible for sending telemetry to the server and correctly
understanding server instructions related to large transfer. If this helper logic
is wrong, the client may:
- send incorrect telemetry,
- misread transfer metadata,
- report wrong transfer status,
- accept unrealistic telemetry values without validation.

- DAL A style relevance:
  Rejecting malformed transfer metadata prevents unsafe or uncontrolled file
  reconstruction behavior.
- DAL B style relevance:
  Telemetry generation directly affects what the server receives and processes.
- DAL C style relevance:
  Transfer status reporting and telemetry range validation support integrity,
  diagnostics, and operational reliability.
- DAL D/E style relevance:
  This file contains meaningful client operational helpers rather than cosmetic
  or purely optional behavior.
*/

namespace agc::client_logic
{
    namespace
    {
        /*
        extractField

        Extracts the value associated with a given key from a semicolon-separated
        metadata string.

        Example input:
        "filename=diagnostic_payload.bin;size=1049600;chunks=257;checksum=1234"

        Example usage:
        extractField(text, "size=", value)  -> value becomes "1049600"

        Importance of this helper:
        The client needs to parse transfer metadata received from the server in
        LARGE_DATA_START packets. This helper isolates the repetitive "find key
        and extract value" logic.

        DAL reasoning:
        This helper supports metadata parsing, so it has indirect DAL A/B-style
        relevance because bad metadata must not be trusted.

        Logic:
        1. Find the position of the requested key.
        2. Determine where the value starts (immediately after the key).
        3. Find the end of the value (next ';' or end of string).
        4. Reject invalid cases such as missing key or empty value.
        5. Extract the substring into the output reference.
        */
        bool extractField(const std::string& text,
            const std::string& key,
            std::string& value)
        {
            // Search for the requested key such as "size=" or "checksum=".
            const std::size_t keyPos = text.find(key);
            if (keyPos == std::string::npos)
            {
                // The key was not present, so the field cannot be extracted.
                return false;
            }

            // The actual value begins immediately after the key text.
            const std::size_t valueStart = keyPos + key.size();

            // Values are separated by ';'. If no semicolon is found, this field
            // is assumed to run until the end of the metadata string.
            std::size_t valueEnd = text.find(';', valueStart);
            if (valueEnd == std::string::npos)
            {
                valueEnd = text.size();
            }

            // Reject invalid ranges such as an empty value.
            if (valueStart >= valueEnd)
            {
                return false;
            }

            // Extract the substring containing only the value.
            value = text.substr(valueStart, valueEnd - valueStart);
            return true;
        }
    }

    /*
    generateTelemetry

    Generates a deterministic telemetry sample for the aircraft client.

    Importance of this function:
    The project simulates aircraft telemetry without relying on actual hardware
    sensors. Using predictable formulas makes communication and testing easier.

    DAL reasoning:
    This is conceptually closest to DAL B/C because telemetry values directly
    affect what the server monitors and acknowledges.

    Logic:
    - altitude increases by 100 per sample
    - speed increases by 2 per sample
    - heading increases by 1 per sample
    - fuel decreases by 1 per sample
    */
    TelemetryData generateTelemetry(std::uint32_t sampleIndex)
    {
        TelemetryData telemetry{};

        // Simulate gradual altitude increase as additional samples are produced.
        telemetry.altitudeFt = 30000.0 + static_cast<double>(sampleIndex * 100U);

        // Simulate gradual speed increase across samples.
        telemetry.speedKnots = 450.0 + static_cast<double>(sampleIndex * 2U);

        // Simulate heading change as flight direction evolves.
        telemetry.headingDeg = 90.0 + static_cast<double>(sampleIndex);

        // Simulate fuel usage over time by decreasing fuel percentage.
        telemetry.fuelPercent = 80.0 - static_cast<double>(sampleIndex);

        return telemetry;
    }

    /*
    parseTransferMetadata

    Parses the metadata string received from the server before large chunked
    transfer begins.

    Importance of this function:
    Before the client can reconstruct the transferred file, it must understand:
    - how large the incoming file should be,
    - how many chunks it should expect,
    - and what checksum it should compare against after reconstruction.

    DAL reasoning:
    This is conceptually closest to DAL A/B because malformed metadata must be
    rejected rather than trusted blindly.

    Logic:
    1. Extract "size=", "chunks=", and "checksum=" fields.
    2. Convert the extracted text values into numeric types.
    3. If conversion fails, reject the metadata.
    4. If everything succeeds, return true.

    Safety point:
    The function uses defensive failure handling. Missing or malformed values
    cause a false return instead of partial acceptance.
    */
    bool parseTransferMetadata(const std::string& metadataText,
        std::size_t& expectedSize,
        std::uint32_t& expectedChunks,
        std::uint32_t& expectedChecksum)
    {
        std::string sizeText{};
        std::string chunksText{};
        std::string checksumText{};

        // Extract the total expected transfer size.
        if (extractField(metadataText, "size=", sizeText) == false)
        {
            return false;
        }

        // Extract the total number of expected chunks.
        if (extractField(metadataText, "chunks=", chunksText) == false)
        {
            return false;
        }

        // Extract the checksum value that will be used later for verification.
        if (extractField(metadataText, "checksum=", checksumText) == false)
        {
            return false;
        }

        try
        {
            // Convert all extracted text values into numeric output fields.
            expectedSize = static_cast<std::size_t>(std::stoull(sizeText));
            expectedChunks = static_cast<std::uint32_t>(std::stoul(chunksText));
            expectedChecksum = static_cast<std::uint32_t>(std::stoul(checksumText));
        }
        catch (...)
        {
            // Any parsing failure means the metadata must be rejected.
            return false;
        }

        return true;
    }

    /*
    buildTransferStatusText

    Builds the structured completion status string sent back to the server after
    large transfer reconstruction and verification.

    Importance of this function:
    After receiving all chunks, the client must report:
    - whether the transfer succeeded,
    - how many bytes were actually received,
    - what checksum was computed from the reconstructed data.

    DAL reasoning:
    This is closest to DAL C because it supports post-transfer validation and
    diagnostics rather than primary session control.

    Logic:
    The function creates a structured key/value text string that can be placed
    inside a STATUS_RESPONSE packet.
    */
    std::string buildTransferStatusText(bool transferOk,
        std::size_t actualSize,
        std::uint32_t actualChecksum)
    {
        return std::string("TRANSFER_OK=") +
            (transferOk ? "TRUE" : "FALSE") +
            ";SIZE=" + std::to_string(actualSize) +
            ";CHECKSUM=" + std::to_string(actualChecksum);
    }

    /*
    isTelemetryWithinExpectedOperationalRange

    Checks whether a telemetry sample lies within the expected operating range
    defined for this project simulation.

    Importance of this function:
    Even though telemetry is generated programmatically, explicit range checks
    still help:
    - verify correctness during testing,
    - catch unrealistic values,
    - support defensive programming.

    DAL reasoning:
    This is conceptually closest to DAL B/C because unrealistic telemetry should
    not silently pass as acceptable operational information.

    Logic:
    The function returns true only if all individual fields satisfy their
    allowed range:
    - altitude: 0 to 50000
    - speed: 0 to 1000
    - heading: 0 to 360
    - fuel: 0 to 100
    */
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