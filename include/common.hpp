#ifndef AIRCRAFT_GROUND_CONTROL_COMMON_HPP
#define AIRCRAFT_GROUND_CONTROL_COMMON_HPP

/*
Purpose: Shared definitions, packet structures, validation, logging, and
         network utility declarations.

Safety Notes:
- Written in a MISRA-conscious C++ style.
- This module avoids unsafe casts where possible, bounds payload sizes,
  validates timestamps, and uses fixed-width integer types.
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
    constexpr std::uint32_t MAGIC_NUMBER = 0x41474331UL;
    constexpr std::uint16_t PROTOCOL_VERSION = 1U;
    constexpr std::uint32_t MAX_PAYLOAD_SIZE = 128U;
    constexpr std::uint64_t MAX_TIMESTAMP_DRIFT_MS = 30000ULL;
    constexpr std::size_t AIRCRAFT_ID_LENGTH = 16U;

    enum class MessageType : std::uint32_t
    {
        CONNECT_REQUEST = 1U,
        CONNECT_ACK     = 2U,
        TELEMETRY       = 3U,
        TELEMETRY_ACK   = 4U,
        ERROR_MESSAGE   = 5U,
        DISCONNECT      = 6U
    };

    enum class ServerState : std::uint32_t
    {
        STARTUP        = 0U,
        IDLE_LISTENING = 1U,
        VERIFICATION   = 2U,
        ACTIVE         = 3U,
        ERROR_STATE    = 4U,
        SHUTDOWN       = 5U
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

    private:
        std::ofstream m_file;
    };

    bool initializeSockets();
    void cleanupSockets();
    void closeSocket(SOCKET socketHandle);

    std::uint64_t getCurrentTimeMs();

    bool sendAll(SOCKET socketHandle, const std::uint8_t* data, std::size_t length);
    bool receiveAll(SOCKET socketHandle, std::uint8_t* data, std::size_t length);

    bool sendPacket(SOCKET socketHandle, const Packet& packet);
    bool receivePacket(SOCKET socketHandle, Packet& packet);

    Packet makePacket(MessageType type,
                      std::uint32_t sequenceNumber,
                      const std::string& aircraftId,
                      const std::vector<std::uint8_t>& payloadData);

    std::string messageTypeToString(MessageType type);
    std::string safeAircraftIdToString(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);

    bool validateAircraftId(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);
    bool validateTimestamp(std::uint64_t packetTimestampMs, std::uint64_t currentTimestampMs);
    bool validatePayloadSize(std::uint32_t payloadSize);

    std::vector<std::uint8_t> telemetryToPayload(const TelemetryData& telemetry);
    bool payloadToTelemetry(const std::vector<std::uint8_t>& payload, TelemetryData& telemetry);

    Packet makeErrorPacket(std::uint32_t sequenceNumber,
                           const std::string& aircraftId,
                           const std::string& errorText);

} /* namespace agc */

#endif /* AIRCRAFT_GROUND_CONTROL_COMMON_HPP */