/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian


This file implements all shared protocol functionality used by both the
aircraft client and the ground control server. It includes:
- Packet serialization/deserialization
- Socket initialization and cleanup
- Reliable send/receive loops
- Logging to CSV
- Validation (aircraft ID, timestamp, payload size)
- Telemetry conversion
- Checksum computation
- Large diagnostic payload generation

Importance of this file:
This file forms the communication backbone of the system. Any mistake in
packet structure, serialization, or validation can break communication or
introduce unsafe behavior.


- DAL A (highest criticality influence):
  Payload size validation, timestamp validation, and safe packet parsing.
- DAL B:
  Packet serialization/deserialization and communication correctness.
- DAL C:
  Logging, telemetry conversion, checksum logic.
- DAL D/E:
  Utility helpers and formatting functions.
*/

#include "common.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace agc
{
    namespace
    {
        /*
        Converts a 64-bit integer from host byte order to network byte order.

        
        Standard functions (htonl) only support 32-bit values. Since timestamps
        are 64-bit, we split into two halves and convert separately.
        */
        std::uint64_t static hostToNetwork64(std::uint64_t value)
        {
            const std::uint32_t highPart =
                htonl(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
            const std::uint32_t lowPart =
                htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

            return (static_cast<std::uint64_t>(lowPart) << 32U) |
                static_cast<std::uint64_t>(highPart);
        }

        /*
        Converts a 64-bit integer from network byte order to host byte order.
        */
        std::uint64_t networkToHost64(std::uint64_t value)
        {
            const std::uint32_t highPart =
                ntohl(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
            const std::uint32_t lowPart =
                ntohl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

            return (static_cast<std::uint64_t>(lowPart) << 32U) |
                static_cast<std::uint64_t>(highPart);
        }

        /*
        Serializes PacketHeader into a byte buffer.

        DAL B:
        This function is critical because incorrect serialization can corrupt
        communication between client and server.
        */
        static bool serializeHeader(const PacketHeader& header, std::vector<std::uint8_t>& output)
        {
            PacketHeader networkHeader = header;

            // Convert all multi-byte fields to network byte order
            networkHeader.magic = htonl(networkHeader.magic);
            networkHeader.version = htons(networkHeader.version);
            networkHeader.reserved = htons(networkHeader.reserved);
            networkHeader.messageType = htonl(networkHeader.messageType);
            networkHeader.sequenceNumber = htonl(networkHeader.sequenceNumber);
            networkHeader.timestampMs = hostToNetwork64(networkHeader.timestampMs);
            networkHeader.payloadSize = htonl(networkHeader.payloadSize);

            output.resize(sizeof(PacketHeader));
            (void)memcpy(output.data(), &networkHeader, sizeof(PacketHeader));

            return true;
        }

        /*
        Deserializes byte buffer into PacketHeader.

        DAL B:
        Must correctly reconstruct packet metadata before payload is processed.
        */
        static bool deserializeHeader(const std::vector<std::uint8_t>& input, PacketHeader& header)
        {
            if (input.size() != sizeof(PacketHeader))
            {
                return false;
            }

            PacketHeader tempHeader{};
            (void)memcpy(&tempHeader, input.data(), sizeof(PacketHeader));

            // Convert from network byte order to host byte order
            tempHeader.magic = ntohl(tempHeader.magic);
            tempHeader.version = ntohs(tempHeader.version);
            tempHeader.reserved = ntohs(tempHeader.reserved);
            tempHeader.messageType = ntohl(tempHeader.messageType);
            tempHeader.sequenceNumber = ntohl(tempHeader.sequenceNumber);
            tempHeader.timestampMs = networkToHost64(tempHeader.timestampMs);
            tempHeader.payloadSize = ntohl(tempHeader.payloadSize);

            header = tempHeader;
            return true;
        }
    }

    /*
    LOGGER IMPLEMENTATION
    */

    Logger::Logger(const std::string& fileName)
        : m_file(fileName, std::ios::out | std::ios::app)
    {
        // Add CSV header for readability
        if (m_file.is_open())
        {
            m_file << "LogTimeMs,Direction,MsgType,Sequence,AircraftId,Source,Destination,PayloadSize,Note\n";
        }
    }

    Logger::~Logger()
    {
        if (m_file.is_open())
        {
            m_file.close();
        }
    }

    bool Logger::isOpen() const
    {
        return m_file.is_open();
    }

    void Logger::logPacket(const std::string& direction,
        const Packet& packet,
        const std::string& source,
        const std::string& destination,
        const std::string& note)
    {
        /*
        DAL C:
        Logging is essential for traceability and debugging but does not directly
        control system behavior.
        */
        if (m_file.is_open())
        {
            m_file << getCurrentTimeMs() << ","
                << direction << ","
                << messageTypeToString(static_cast<MessageType>(packet.header.messageType)) << ","
                << packet.header.sequenceNumber << ","
                << safeAircraftIdToString(packet.header.aircraftId) << ","
                << source << ","
                << destination << ","
                << packet.header.payloadSize << ","
                << note << "\n";
        }
    }

    void Logger::logText(const std::string& note)
    {
        if (m_file.is_open())
        {
            m_file << getCurrentTimeMs() << ",TEXT,N/A,N/A,N/A,N/A,N/A,N/A," << note << "\n";
        }
    }

    void Logger::logError(const std::string& errorCode, const std::string& note)
    {
        if (m_file.is_open())
        {
            m_file << getCurrentTimeMs() << ",ERROR," << errorCode
                << ",N/A,N/A,N/A,N/A,N/A," << note << "\n";
        }
    }

    /*
    SOCKET MANAGEMENT
    */

    bool initializeSockets()
    {
        WSADATA wsaData{};
        return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    }

    void cleanupSockets()
    {
        int cleanupResult = WSACleanup();
        (void)cleanupResult;
    }

    void closeSocket(SOCKET socketHandle)
    {
        if (socketHandle != INVALID_SOCKET)
        {
            (void)closesocket(socketHandle);
        }
    }

    bool setSocketTimeouts(SOCKET socketHandle, std::uint32_t timeoutMs)
    {
        const int timeoutValue = static_cast<int>(timeoutMs);

        const int recvResult = setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutValue),
            sizeof(timeoutValue));

        const int sendResult = setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeoutValue),
            sizeof(timeoutValue));

        return ((recvResult == 0) && (sendResult == 0));
    }

    bool waitForReadable(SOCKET socketHandle, std::uint32_t timeoutMs)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketHandle, &readSet);

        timeval timeout{};
        timeout.tv_sec = static_cast<long>(timeoutMs / 1000U);
        timeout.tv_usec = static_cast<long>((timeoutMs % 1000U) * 1000U);

        const int result = select(0, &readSet, nullptr, nullptr, &timeout);

        return (result > 0);
    }

    std::uint64_t getCurrentTimeMs()
    {
        const auto now = std::chrono::system_clock::now();
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        return static_cast<std::uint64_t>(millis);
    }

    /*
    SEND / RECEIVE (CRITICAL LOGIC)
    */

    bool sendAll(SOCKET socketHandle, const std::uint8_t* data, std::size_t length)
    {
        /*
        CRITICAL LOGIC:
        TCP send() may not send all bytes in one call.
        This loop ensures full transmission.
        */
        std::size_t totalSent = 0U;

        while (totalSent < length)
        {
            const int sent = send(socketHandle,
                reinterpret_cast<const char*>(data + totalSent),
                static_cast<int>(length - totalSent),
                0);

            if (sent <= 0)
            {
                return false;
            }

            totalSent += static_cast<std::size_t>(sent);
        }

        return true;
    }

    bool receiveAll(SOCKET socketHandle, std::uint8_t* data, std::size_t length)
    {
        /*
        CRITICAL LOGIC:
        TCP recv() may return partial data.
        This ensures we receive the exact expected number of bytes.
        */
        std::size_t totalReceived = 0U;

        while (totalReceived < length)
        {
            const int received = recv(socketHandle,
                reinterpret_cast<char*>(data + totalReceived),
                static_cast<int>(length - totalReceived),
                0);

            if (received <= 0)
            {
                return false;
            }

            totalReceived += static_cast<std::size_t>(received);
        }

        return true;
    }

    /*
makePacket

Purpose:
Creates a fully populated protocol packet using the supplied message type,
sequence number, aircraft ID, and payload data.

Importance of this function:
Both client and server repeatedly need to create valid protocol packets.
Centralizing packet construction ensures consistency of:
- magic number
- protocol version
- timestamp
- payload size
- aircraft ID formatting

DAL reasoning:
This is conceptually closest to DAL B because incorrect packet construction
directly affects core communication behavior between the two applications.
*/
    Packet makePacket(MessageType type,
        std::uint32_t sequenceNumber,
        const std::string& aircraftId,
        const std::vector<std::uint8_t>& payloadData)
    {
        Packet packet{};
        packet.header.magic = MAGIC_NUMBER;
        packet.header.version = PROTOCOL_VERSION;
        packet.header.reserved = 0U;
        packet.header.messageType = static_cast<std::uint32_t>(type);
        packet.header.sequenceNumber = sequenceNumber;
        packet.header.timestampMs = getCurrentTimeMs();
        packet.header.payloadSize = static_cast<std::uint32_t>(payloadData.size());
        packet.header.aircraftId.fill('\0');

        // Copy the aircraft ID safely into the fixed-size header field.
        const std::size_t copyLength =
            (aircraftId.size() < (AIRCRAFT_ID_LENGTH - 1U))
            ? aircraftId.size()
            : (AIRCRAFT_ID_LENGTH - 1U);

        for (std::size_t i = 0U; i < copyLength; ++i)
        {
            packet.header.aircraftId[i] = aircraftId[i];
        }

        packet.payload = payloadData;
        return packet;
    }

    /*
    makeErrorPacket

    Purpose:
    Creates a protocol error packet containing the supplied error text.

    Importance of this function:
    The server and client may need to report structured protocol errors instead
    of failing silently. This helper keeps error-packet creation consistent.

    DAL reasoning:
    This is conceptually closest to DAL B/C because structured error reporting
    supports controlled fault handling and traceability.
    */
    Packet makeErrorPacket(std::uint32_t sequenceNumber,
        const std::string& aircraftId,
        const std::string& errorText)
    {
        return makePacket(MessageType::ERROR_MESSAGE,
            sequenceNumber,
            aircraftId,
            stringToPayload(errorText));
    }

    bool sendPacket(SOCKET socketHandle, const Packet& packet)
    {
        /*
        DAL A:
        Validate payload size before sending to prevent protocol misuse.
        */
        if (validatePayloadSize(packet.header.payloadSize) == false)
        {
            return false;
        }

        std::vector<std::uint8_t> headerBytes{};
        if (!serializeHeader(packet.header, headerBytes))
        {
            return false;
        }

        if (!sendAll(socketHandle, headerBytes.data(), headerBytes.size()))
        {
            return false;
        }

        if (!packet.payload.empty())
        {
            if (!sendAll(socketHandle, packet.payload.data(), packet.payload.size()))
            {
                return false;
            }
        }

        return true;
    }

    bool receivePacket(SOCKET socketHandle, Packet& packet)
    {
        std::vector<std::uint8_t> headerBytes(sizeof(PacketHeader), 0U);

        if (!receiveAll(socketHandle, headerBytes.data(), headerBytes.size()))
        {
            return false;
        }

        PacketHeader header{};
        if (!deserializeHeader(headerBytes, header))
        {
            return false;
        }

        /*
        CRITICAL VALIDATION (DAL A):
        Reject invalid packets early.
        */
        if ((header.magic != MAGIC_NUMBER) ||
            (header.version != PROTOCOL_VERSION) ||
            (!validatePayloadSize(header.payloadSize)))
        {
            return false;
        }

        packet.header = header;
        packet.payload.clear();

        if (header.payloadSize > 0U)
        {
            packet.payload.resize(header.payloadSize);

            if (!receiveAll(socketHandle, packet.payload.data(), packet.payload.size()))
            {
                return false;
            }
        }

        return true;
    }

    /*
    UTILITY FUNCTIONS
    */

    std::string messageTypeToString(MessageType type)
    {
        switch (type)
        {
        case MessageType::CONNECT_REQUEST:
        {
            return "CONNECT_REQUEST";
            break;
        }
        case MessageType::CONNECT_ACK:
        {
            return "CONNECT_ACK";
            break;
        }
        case MessageType::TELEMETRY:
        {
            return "TELEMETRY";
            break;
        }
        case MessageType::TELEMETRY_ACK:
        {
            return "TELEMETRY_ACK";
            break;
        }
        case MessageType::ERROR_MESSAGE:
        {
            return "ERROR_MESSAGE";
            break;
        }
        case MessageType::DISCONNECT:
        {
            return "DISCONNECT";
            break;
        }
        case MessageType::COMMAND:
        {
            return "COMMAND";
            break;
        }
        case MessageType::COMMAND_ACK:
        {
            return "COMMAND_ACK";
            break;
        }
        case MessageType::DATA_REQUEST:
        {
            return "DATA_REQUEST";
            break;
        }
        case MessageType::STATUS_RESPONSE:
        {
            return "STATUS_RESPONSE";
            break;
        }
        case MessageType::LARGE_DATA_START:
        {
            return "LARGE_DATA_START";
            break;
        }
        case MessageType::LARGE_DATA_CHUNK:
        {
            return "LARGE_DATA_CHUNK";
            break;
        }
        case MessageType::LARGE_DATA_END:
        {
            return "LARGE_DATA_END";
            break;
        }
        default:
        {
            return "UNKNOWN";
            break;
        }
        }
    }

    std::string safeAircraftIdToString(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId)
    {
        std::string result;

        for (char c : aircraftId)
        {
            if (c == '\0') break;
            result.push_back(c);
        }

        return result;
    }

    bool validateAircraftId(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId)
    {
        return (safeAircraftIdToString(aircraftId) == "ACFT001");
    }

    bool validateTimestamp(std::uint64_t packetTimestampMs, std::uint64_t currentTimestampMs)
    {
        std::uint64_t delta =
            (currentTimestampMs > packetTimestampMs)
            ? (currentTimestampMs - packetTimestampMs)
            : (packetTimestampMs - currentTimestampMs);

        return (delta <= MAX_TIMESTAMP_DRIFT_MS);
    }

    bool validatePayloadSize(std::uint32_t payloadSize)
    {
        return (payloadSize <= MAX_PAYLOAD_SIZE);
    }

    /*
    TELEMETRY + DATA UTILITIES
    */

    std::vector<std::uint8_t> telemetryToPayload(const TelemetryData& telemetry)
    {
        std::ostringstream stream{};
        stream << std::fixed << std::setprecision(2)
            << telemetry.altitudeFt << ","
            << telemetry.speedKnots << ","
            << telemetry.headingDeg << ","
            << telemetry.fuelPercent;

        const std::string text = stream.str();
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    bool payloadToTelemetry(const std::vector<std::uint8_t>& payload, TelemetryData& telemetry)
    {
        const std::string text(payload.begin(), payload.end());
        std::stringstream stream(text);

        char s1, s2, s3;

        if ((stream >> telemetry.altitudeFt >> s1
            >> telemetry.speedKnots >> s2
            >> telemetry.headingDeg >> s3
            >> telemetry.fuelPercent) &&
            (s1 == ',' && s2 == ',' && s3 == ','))
        {
            return true;
        }

        return false;
    }

    std::vector<std::uint8_t> stringToPayload(const std::string& text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    std::string payloadToString(const std::vector<std::uint8_t>& payload)
    {
        return std::string(payload.begin(), payload.end());
    }

    std::uint32_t computeChecksum(const std::vector<std::uint8_t>& data)
    {
        std::uint32_t checksum = 0U;

        for (auto byte : data)
        {
            checksum += byte;
        }

        return checksum;
    }

    std::vector<std::uint8_t> generateDiagnosticBlob(std::size_t totalSize)
    {
        /*
        Generates deterministic pseudo-data for testing large transfer.
        No external file dependency → more reliable integration tests.
        */
        std::vector<std::uint8_t> data(totalSize);

        for (std::size_t i = 0; i < totalSize; ++i)
        {
            data[i] = static_cast<std::uint8_t>(i % 251);
        }

        return data;
    }
}