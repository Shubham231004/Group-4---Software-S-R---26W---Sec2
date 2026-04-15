#ifndef AIRCRAFT_GROUND_CONTROL_COMMON_HPP
#define AIRCRAFT_GROUND_CONTROL_COMMON_HPP

/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian
File     : common.hpp

Purpose:
This header defines the shared protocol contract used by both the aircraft
client and the ground control server. It centralizes:
- protocol constants
- packet/message definitions
- server state definitions
- shared data structures
- logging interface
- socket helper declarations
- serialization/deserialization declarations
- validation helper declarations

Importance of this file:
Both endpoints must interpret the exact same packet format and protocol rules.
If this shared contract becomes inconsistent, communication can fail, data may
be misinterpreted, and the entire distributed system can become unreliable.

DAL-oriented commentary (engineering rationale only, not certification claim):
- DAL A style relevance:
  Payload-size bounding, aircraft ID validation, and timestamp validation act as
  defensive barriers against unsafe or uncontrolled communication input.
- DAL B style relevance:
  Message types and server states directly influence major distributed behavior.
- DAL C style relevance:
  Logging, telemetry conversion, checksum support, and transfer helpers support
  reliable operation and traceability.
- DAL D/E style relevance:
  Supportive or convenience-oriented declarations are lower direct criticality
  than validation and control-flow definitions.
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
    /*
    Shared protocol constants.
    These must remain synchronized between client and server because both sides
    use them to interpret packet structure and communication rules consistently.
    */
    constexpr std::uint16_t SERVER_PORT = 5050U;

    // Protocol signature used to identify valid AGC packets.
    constexpr std::uint32_t MAGIC_NUMBER = 0x41474331UL; // "AGC1"

    // Version number for protocol compatibility checks.
    constexpr std::uint16_t PROTOCOL_VERSION = 2U;

    /*
    Upper bound on any packet payload size.

    DAL A/B style reasoning:
    Bounding payload size reduces the risk of malformed or excessive packet data
    causing uncontrolled allocation, unsafe parsing, or protocol misuse.
    */
    constexpr std::uint32_t MAX_PAYLOAD_SIZE = 8192U;

    // Maximum allowed timestamp drift when validating packets.
    constexpr std::uint64_t MAX_TIMESTAMP_DRIFT_MS = 30000ULL;

    // Fixed aircraft ID storage size used in the packet header.
    constexpr std::size_t AIRCRAFT_ID_LENGTH = 16U;

    // Standard socket timeout used for send/receive operations.
    constexpr std::uint32_t SOCKET_TIMEOUT_MS = 5000U;

    // Short timeout used when checking for optional server messages.
    constexpr std::uint32_t OPTIONAL_WAIT_MS = 750U;

    // Maximum number of connection attempts before giving up.
    constexpr std::uint32_t MAX_RECONNECT_ATTEMPTS = 3U;

    // Maximum number of retransmission attempts for important protocol steps.
    constexpr std::uint32_t MAX_RETRANSMISSION_ATTEMPTS = 3U;

    /*
    Large data transfer settings.
    The total size is intentionally greater than 1 MB to satisfy the project
    requirement for server-to-client transfer of a large object.
    */
    constexpr std::size_t LARGE_FILE_SIZE_BYTES = 1049600U;
    constexpr std::size_t LARGE_FILE_CHUNK_SIZE = 4096U;

    /*
    MessageType
    Defines all supported protocol message categories.

    DAL B/C style reasoning:
    These values determine how each endpoint interprets and reacts to incoming
    packets. If the mapping is wrong, major communication behavior breaks.
    */
    enum class MessageType : std::uint32_t
    {
        CONNECT_REQUEST = 1U,
        CONNECT_ACK = 2U,
        TELEMETRY = 3U,
        TELEMETRY_ACK = 4U,
        ERROR_MESSAGE = 5U,
        DISCONNECT = 6U,
        COMMAND = 7U,
        COMMAND_ACK = 8U,
        DATA_REQUEST = 9U,
        STATUS_RESPONSE = 10U,
        LARGE_DATA_START = 11U,
        LARGE_DATA_CHUNK = 12U,
        LARGE_DATA_END = 13U
    };

    /*
    ServerState
    Defines the server-side operational state machine.

    DAL B style reasoning:
    The server uses these states to restrict actions to valid communication
    phases, helping enforce controlled and predictable behavior.
    */
    enum class ServerState : std::uint32_t
    {
        STARTUP = 0U,
        IDLE_LISTENING = 1U,
        VERIFICATION = 2U,
        ACTIVE = 3U,
        COMMAND_PROCESSING = 4U,
        DATA_TRANSFER = 5U,
        DISCONNECTING = 6U,
        ERROR_STATE = 7U,
        SHUTDOWN = 8U
    };

    /*
    PacketHeader:
    Fixed-size protocol metadata written before the payload. This allows the
    receiver to identify and validate the packet before interpreting its body.
    */
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

    /*
    Packet:
    Full communication unit used by the protocol.

    The payload is dynamically allocated using std::vector, which satisfies the
    project requirement that the packet structure include at least one dynamic
    element.
    */
    struct Packet
    {
        PacketHeader header;
        std::vector<std::uint8_t> payload;
    };

    // High-level telemetry values before they are serialized into payload form.
    struct TelemetryData
    {
        double altitudeFt;
        double speedKnots;
        double headingDeg;
        double fuelPercent;
    };

    /*
    Logger:
    Writes packet and event data to a CSV file. File-based logging is an explicit
    project requirement and is important for auditability and debugging.
    */
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

    // Socket lifecycle helpers.
    bool initializeSockets();
    void cleanupSockets();
    void closeSocket(SOCKET socketHandle);

    // Socket timing helpers.
    bool setSocketTimeouts(SOCKET socketHandle, std::uint32_t timeoutMs);
    bool waitForReadable(SOCKET socketHandle, std::uint32_t timeoutMs);

    // Shared millisecond time source used for timestamps and logs.
    std::uint64_t getCurrentTimeMs();

    // Low-level buffer send/receive loops.
    bool sendAll(SOCKET socketHandle, const std::uint8_t* data, std::size_t length);
    bool receiveAll(SOCKET socketHandle, std::uint8_t* data, std::size_t length);

    // High-level packet send/receive helpers.
    bool sendPacket(SOCKET socketHandle, const Packet& packet);
    bool receivePacket(SOCKET socketHandle, Packet& packet);

    // Packet builders.
    Packet makePacket(MessageType type,
        std::uint32_t sequenceNumber,
        const std::string& aircraftId,
        const std::vector<std::uint8_t>& payloadData);

    Packet makeErrorPacket(std::uint32_t sequenceNumber,
        const std::string& aircraftId,
        const std::string& errorText);

    // Utility conversions and validation helpers.
    std::string messageTypeToString(MessageType type);
    std::string safeAircraftIdToString(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);

    bool validateAircraftId(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId);
    bool validateTimestamp(std::uint64_t packetTimestampMs, std::uint64_t currentTimestampMs);
    bool validatePayloadSize(std::uint32_t payloadSize);

    // Telemetry conversion helpers.
    std::vector<std::uint8_t> telemetryToPayload(const TelemetryData& telemetry);
    bool payloadToTelemetry(const std::vector<std::uint8_t>& payload, TelemetryData& telemetry);

    // Generic text/payload conversion helpers.
    std::vector<std::uint8_t> stringToPayload(const std::string& text);
    std::string payloadToString(const std::vector<std::uint8_t>& payload);

    // Integrity and large-transfer helpers.
    std::uint32_t computeChecksum(const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> generateDiagnosticBlob(std::size_t totalSize);
}

#endif