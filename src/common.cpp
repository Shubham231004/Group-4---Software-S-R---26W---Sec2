/*
CSCN74000-26W-Sec2-Software Safety &v reliability
Project   : Aircraft-Ground Control Communication System
Group 4: Shubham, Brian, Yinus

Purpose   : Shared implementation for packet handling, logging, validation,
            serialization, and telemetry conversion.

Critical Sections:
1. Packet serialization/deserialization:
   Any mistake here can corrupt communication.
2. Socket send/receive loops:
   Partial sends/receives must be handled safely.
3. Validation logic:
   Packet timestamp, payload size, and aircraft ID are checked defensively.
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
        std::uint64_t hostToNetwork64(std::uint64_t value)
        {
            const std::uint32_t highPart =
                htonl(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
            const std::uint32_t lowPart =
                htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

            return (static_cast<std::uint64_t>(lowPart) << 32U) |
                   static_cast<std::uint64_t>(highPart);
        }

        std::uint64_t networkToHost64(std::uint64_t value)
        {
            const std::uint32_t highPart =
                ntohl(static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
            const std::uint32_t lowPart =
                ntohl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));

            return (static_cast<std::uint64_t>(lowPart) << 32U) |
                   static_cast<std::uint64_t>(highPart);
        }

        bool serializeHeader(const PacketHeader& header, std::vector<std::uint8_t>& output)
        {
            PacketHeader networkHeader = header;
            networkHeader.magic = htonl(networkHeader.magic);
            networkHeader.version = htons(networkHeader.version);
            networkHeader.reserved = htons(networkHeader.reserved);
            networkHeader.messageType = htonl(networkHeader.messageType);
            networkHeader.sequenceNumber = htonl(networkHeader.sequenceNumber);
            networkHeader.timestampMs = hostToNetwork64(networkHeader.timestampMs);
            networkHeader.payloadSize = htonl(networkHeader.payloadSize);

            const std::size_t headerSize = sizeof(PacketHeader);
            output.resize(headerSize);
            std::memcpy(output.data(), &networkHeader, headerSize);

            return true;
        }

        bool deserializeHeader(const std::vector<std::uint8_t>& input, PacketHeader& header)
        {
            if (input.size() != sizeof(PacketHeader))
            {
                return false;
            }

            PacketHeader tempHeader{};
            std::memcpy(&tempHeader, input.data(), sizeof(PacketHeader));

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

    Logger::Logger(const std::string& fileName)
        : m_file(fileName, std::ios::out | std::ios::app)
    {
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
        if (m_file.is_open())
        {
            m_file
                << getCurrentTimeMs() << ","
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

    bool initializeSockets()
    {
        WSADATA wsaData{};
        return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    }

    void cleanupSockets()
    {
        WSACleanup();
    }

    void closeSocket(SOCKET socketHandle)
    {
        if (socketHandle != INVALID_SOCKET)
        {
            (void)closesocket(socketHandle);
        }
    }

    std::uint64_t getCurrentTimeMs()
    {
        const auto now = std::chrono::system_clock::now();
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        return static_cast<std::uint64_t>(millis);
    }

    bool sendAll(SOCKET socketHandle, const std::uint8_t* data, std::size_t length)
    {
        std::size_t totalSent = 0U;

        while (totalSent < length)
        {
            const int remaining = static_cast<int>(length - totalSent);
            const int sent = send(socketHandle,
                                  reinterpret_cast<const char*>(data + totalSent),
                                  remaining,
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
        std::size_t totalReceived = 0U;

        while (totalReceived < length)
        {
            const int remaining = static_cast<int>(length - totalReceived);
            const int received = recv(socketHandle,
                                      reinterpret_cast<char*>(data + totalReceived),
                                      remaining,
                                      0);

            if (received <= 0)
            {
                return false;
            }

            totalReceived += static_cast<std::size_t>(received);
        }

        return true;
    }

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

        const std::size_t copyLength =
            (aircraftId.size() < (AIRCRAFT_ID_LENGTH - 1U)) ? aircraftId.size() : (AIRCRAFT_ID_LENGTH - 1U);

        for (std::size_t i = 0U; i < copyLength; ++i)
        {
            packet.header.aircraftId[i] = aircraftId[i];
        }

        packet.payload = payloadData;
        return packet;
    }

    bool sendPacket(SOCKET socketHandle, const Packet& packet)
    {
        if (validatePayloadSize(packet.header.payloadSize) == false)
        {
            return false;
        }

        std::vector<std::uint8_t> headerBytes{};
        if (serializeHeader(packet.header, headerBytes) == false)
        {
            return false;
        }

        if (sendAll(socketHandle, headerBytes.data(), headerBytes.size()) == false)
        {
            return false;
        }

        if (packet.payload.empty() == false)
        {
            if (sendAll(socketHandle, packet.payload.data(), packet.payload.size()) == false)
            {
                return false;
            }
        }

        return true;
    }

    bool receivePacket(SOCKET socketHandle, Packet& packet)
    {
        std::vector<std::uint8_t> headerBytes(sizeof(PacketHeader), 0U);

        if (receiveAll(socketHandle, headerBytes.data(), headerBytes.size()) == false)
        {
            return false;
        }

        PacketHeader header{};
        if (deserializeHeader(headerBytes, header) == false)
        {
            return false;
        }

        if ((header.magic != MAGIC_NUMBER) ||
            (header.version != PROTOCOL_VERSION) ||
            (validatePayloadSize(header.payloadSize) == false))
        {
            return false;
        }

        packet.header = header;
        packet.payload.clear();

        if (header.payloadSize > 0U)
        {
            packet.payload.resize(header.payloadSize, 0U);

            if (receiveAll(socketHandle, packet.payload.data(), packet.payload.size()) == false)
            {
                return false;
            }
        }

        return true;
    }

    std::string messageTypeToString(MessageType type)
    {
        switch (type)
        {
            case MessageType::CONNECT_REQUEST:
                return "CONNECT_REQUEST";
            case MessageType::CONNECT_ACK:
                return "CONNECT_ACK";
            case MessageType::TELEMETRY:
                return "TELEMETRY";
            case MessageType::TELEMETRY_ACK:
                return "TELEMETRY_ACK";
            case MessageType::ERROR_MESSAGE:
                return "ERROR_MESSAGE";
            case MessageType::DISCONNECT:
                return "DISCONNECT";
            default:
                return "UNKNOWN";
        }
    }

    std::string safeAircraftIdToString(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId)
    {
        std::string result{};

        for (std::size_t i = 0U; i < AIRCRAFT_ID_LENGTH; ++i)
        {
            if (aircraftId[i] == '\0')
            {
                break;
            }

            result.push_back(aircraftId[i]);
        }

        return result;
    }

    bool validateAircraftId(const std::array<char, AIRCRAFT_ID_LENGTH>& aircraftId)
    {
        return (safeAircraftIdToString(aircraftId) == "ACFT001");
    }

    bool validateTimestamp(std::uint64_t packetTimestampMs, std::uint64_t currentTimestampMs)
    {
        std::uint64_t delta = 0ULL;

        if (currentTimestampMs >= packetTimestampMs)
        {
            delta = currentTimestampMs - packetTimestampMs;
        }
        else
        {
            delta = packetTimestampMs - currentTimestampMs;
        }

        return (delta <= MAX_TIMESTAMP_DRIFT_MS);
    }

    bool validatePayloadSize(std::uint32_t payloadSize)
    {
        return (payloadSize <= MAX_PAYLOAD_SIZE);
    }

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

        char separator1 = '\0';
        char separator2 = '\0';
        char separator3 = '\0';

        if ((stream >> telemetry.altitudeFt >> separator1
                    >> telemetry.speedKnots >> separator2
                    >> telemetry.headingDeg >> separator3
                    >> telemetry.fuelPercent) &&
            (separator1 == ',') &&
            (separator2 == ',') &&
            (separator3 == ','))
        {
            return true;
        }

        return false;
    }

    Packet makeErrorPacket(std::uint32_t sequenceNumber,
                           const std::string& aircraftId,
                           const std::string& errorText)
    {
        const std::vector<std::uint8_t> payload(errorText.begin(), errorText.end());
        return makePacket(MessageType::ERROR_MESSAGE, sequenceNumber, aircraftId, payload);
    }

} /* namespace agc */