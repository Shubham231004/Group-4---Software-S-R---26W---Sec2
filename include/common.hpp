// Updated for sprint 2

#ifndef AIRCRAFT_GROUND_CONTROL_COMMON_HPP
#define AIRCRAFT_GROUND_CONTROL_COMMON_HPP

/*
Project   : Aircraft-Ground Control Communication System
Sprint    : Sprint 2
Course    : Software Safety and Reliability

Purpose:
Shared protocol, data structures, validation, logging, timeout helpers,
serialization helpers, and transfer utilities used by both server and client.

Safety / Reliability Notes:
- Fixed-width integer types are used for predictable packet layout.
- Payload sizes are bounded.
- Timestamps, IDs, and message flows are validated.
- Timeouts are applied to reduce indefinite blocking.
- Full MISRA compliance will still be confirmed using PVS Studio static analysis.
*/

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace agc
{
    constexpr std::uint16_t SERVER_PORT = 5050U;
    constexpr std::uint32_t MAGIC_NUMBER = 0x41474331UL; /* "AGC1" */
    constexpr std::uint16_t PROTOCOL_VERSION = 2U;

    constexpr std::uint32_t MAX_PAYLOAD_SIZE = 8192U;
    constexpr std::uint64_t MAX_TIMESTAMP_DRIFT_MS = 30000ULL;
    constexpr std::size_t AIRCRAFT_ID_LENGTH = 16U;

    constexpr std::uint32_t SOCKET_TIMEOUT_MS = 5000U;
    constexpr std::uint32_t OPTIONAL_WAIT_MS = 750U;
    constexpr std::uint32_t MAX_RECONNECT_ATTEMPTS = 3U;
    constexpr std::uint32_t MAX_RETRANSMISSION_ATTEMPTS = 3U;

    constexpr std::size_t LARGE_FILE_SIZE_BYTES = 1049600U; // > 1 MB //
    constexpr std::size_t LARGE_FILE_CHUNK_SIZE = 4096U;

    enum class MessageType : std::uint32_t
    {
        CONNECT_REQUEST   = 1U,
        CONNECT_ACK       = 2U,
        TELEMETRY         = 3U,
        TELEMETRY_ACK     = 4U,
        ERROR_MESSAGE     = 5U,
        DISCONNECT        = 6U,
        COMMAND           = 7U,
        COMMAND_ACK       = 8U,
        DATA_REQUEST      = 9U,
        STATUS_RESPONSE   = 10U,
        LARGE_DATA_START  = 11U,
        LARGE_DATA_CHUNK  = 12U,
        LARGE_DATA_END    = 13U
    };

    enum class ServerState : std::uint32_t
    {
        STARTUP                = 0U,
        IDLE_LISTENING         = 1U,
        VERIFICATION           = 2U,
        ACTIVE                 = 3U,
        COMMAND_PROCESSING     = 4U,
        DATA_TRANSFER          = 5U,
        DISCONNECTING          = 6U,
        ERROR_STATE            = 7U,
        SHUTDOWN               = 8U
    };

    struct PacketHeader
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t reserved;
        std::uint32_t messageType;
        std::uint32_t sequenceNumber;
        std::uint64_t timestampMs;
        std::uint32_t payloadSize;
        std::array<char, AIRCRAFT_ID_LENGTH> aircraftId;
    };

    struct Packet
    {
        PacketHeader header;
        std::vector<std::uint8_t> payload;
    };

    struct TelemetryData
    {
        double altitudeFt;
        double speedKnots;
        double headingDeg;
        double fuelPercent;
    };

    class Logger
    {
    public:
        explicit Logger(const std::string& fileName);
        ~Logger();

        bool isOpen() const;

        void logPacket(const std::string& direction,
                       const Packet& packet,
                       const std::string& source,
                       const std::string& destination,
                       const std::string& note);

        void logText(const std::string& note);
        void logError(const std::string& errorCode, const std::string& note);

    private:
        std::ofstream m_file;
    };

    bool initializeSockets();
    void cleanupSockets();
    void closeSocket(SOCKET socketHandle);

    bool setSocketTimeouts(SOCKET socketHandle, std::uint32_t timeoutMs);
    bool waitForReadable(SOCKET socketHandle, std::uint32_t timeoutMs);

    std::uint64_t getCurrentTimeMs();

    bool sendAll(SOCKET socketHandle, const std::uint8_t* data, std::size_t length);
    bool receiveAll(SOCKET socketHandle, std::uint8_t* data, std::size_t length);

    bool sendPacket(SOCKET socketHandle, const Packet& packet);
    bool receivePacket(SOCKET socketHandle, Packet& packet);

    Packet makePacket(MessageType type,
                      std::uint32_t sequenceNumber,
                      const std::string& aircraftId,
                      const std::vector<std::uint8_t>& payloadData);

    Packet makeErrorPacket(std::uint32_t sequenceNumber,
                           const std::string& aircraftId,
                           const std::string& errorText);

    std::string messageTypeToString(MessageType type);
    std::string safeAircraftIdToString(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);

    bool validateAircraftId(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);
    bool validateTimestamp(std::uint64_t packetTimestampMs, std::uint64_t currentTimestampMs);
    bool validatePayloadSize(std::uint32_t payloadSize);

    std::vector<std::uint8_t> telemetryToPayload(const TelemetryData& telemetry);
    bool payloadToTelemetry(const std::vector<std::uint8_t>& payload, TelemetryData& telemetry);

    std::vector<std::uint8_t> stringToPayload(const std::string& text);
    std::string payloadToString(const std::vector<std::uint8_t>& payload);

    std::uint32_t computeChecksum(const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> generateDiagnosticBlob(std::size_t totalSize);

}

#endif