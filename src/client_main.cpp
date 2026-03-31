///*
//This file serves as the Aircraft Client for Sprint 1. Establishes TCP connection, sends connect request, transmits telemetry periodically, validates server responses, logs activity, and disconnects gracefully.
//
//Critical Sections:
//1. Initial handshake: The client must wait for verification before sending telemetry.
//2. Telemetry send/ack cycle: Each sent packet must be confirmed before the next step.
//3. Connection loss handling: Any send/receive failure is treated as a controlled communication failure.
//*/
//
//#include "common.hpp"
//
//#include <chrono>
//#include <iostream>
//#include <thread>
//
//namespace
//{
//    agc::TelemetryData generateTelemetry(std::uint32_t sampleIndex)
//    {
//        agc::TelemetryData telemetry{};
//        telemetry.altitudeFt = 30000.0 + static_cast<double>(sampleIndex * 100U);
//        telemetry.speedKnots = 450.0 + static_cast<double>(sampleIndex * 2U);
//        telemetry.headingDeg = 90.0 + static_cast<double>(sampleIndex * 1U);
//        telemetry.fuelPercent = 80.0 - static_cast<double>(sampleIndex * 1U);
//        return telemetry;
//    }
//}
//
//int main()
//{
//    if (agc::initializeSockets() == false)
//    {
//        std::cerr << "[CLIENT] ERROR: WSAStartup failed.\n";
//        return 1;
//    }
//
//    agc::Logger logger("client_log.csv");
//
//    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//    if (clientSocket == INVALID_SOCKET)
//    {
//        std::cerr << "[CLIENT] ERROR: socket creation failed.\n";
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    sockaddr_in serverAddress{};
//    serverAddress.sin_family = AF_INET;
//    serverAddress.sin_port = htons(agc::SERVER_PORT);
//
//    if (inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr) != 1)
//    {
//        std::cerr << "[CLIENT] ERROR: invalid server address.\n";
//        agc::closeSocket(clientSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    std::cout << "[CLIENT] Connecting to server...\n";
//    if (connect(clientSocket,
//                reinterpret_cast<sockaddr*>(&serverAddress),
//                static_cast<int>(sizeof(serverAddress))) == SOCKET_ERROR)
//    {
//        std::cerr << "[CLIENT] ERROR: connect failed.\n";
//        agc::closeSocket(clientSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    const std::string aircraftId = "ACFT001";
//    std::uint32_t sequenceNumber = 1U;
//
//    {
//        const std::string connectText = "Aircraft requesting connection.";
//        const std::vector<std::uint8_t> payload(connectText.begin(), connectText.end());
//
//        agc::Packet connectPacket =
//            agc::makePacket(agc::MessageType::CONNECT_REQUEST,
//                            sequenceNumber,
//                            aircraftId,
//                            payload);
//
//        if (agc::sendPacket(clientSocket, connectPacket) == false)
//        {
//            std::cerr << "[CLIENT] ERROR: failed to send connect request.\n";
//            logger.logText("Failed to send connect request.");
//            agc::closeSocket(clientSocket);
//            agc::cleanupSockets();
//            return 1;
//        }
//
//        logger.logPacket("TX", connectPacket, "CLIENT", "SERVER", "Connect request sent.");
//        sequenceNumber++;
//    }
//
//    agc::Packet responsePacket{};
//    if (agc::receivePacket(clientSocket, responsePacket) == false)
//    {
//        std::cerr << "[CLIENT] ERROR: no response from server after connect request.\n";
//        logger.logText("No response from server after connect request.");
//        agc::closeSocket(clientSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    logger.logPacket("RX", responsePacket, "SERVER", "CLIENT", "Handshake response received.");
//
//    if (static_cast<agc::MessageType>(responsePacket.header.messageType) !=
//        agc::MessageType::CONNECT_ACK)
//    {
//        std::cerr << "[CLIENT] ERROR: connection not acknowledged by server.\n";
//        logger.logText("Connection not acknowledged by server.");
//        agc::closeSocket(clientSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    std::cout << "[CLIENT] Connection verified by server.\n";
//
//    for (std::uint32_t index = 0U; index < 5U; ++index)
//    {
//        const agc::TelemetryData telemetry = generateTelemetry(index);
//        const std::vector<std::uint8_t> payload = agc::telemetryToPayload(telemetry);
//
//        agc::Packet telemetryPacket =
//            agc::makePacket(agc::MessageType::TELEMETRY,
//                            sequenceNumber,
//                            aircraftId,
//                            payload);
//
//        std::cout << "[CLIENT] Sending telemetry packet #" << sequenceNumber << "\n";
//
//        if (agc::sendPacket(clientSocket, telemetryPacket) == false)
//        {
//            std::cerr << "[CLIENT] ERROR: telemetry send failed. Connection may be lost.\n";
//            logger.logText("Telemetry send failed.");
//            agc::closeSocket(clientSocket);
//            agc::cleanupSockets();
//            return 1;
//        }
//
//        logger.logPacket("TX", telemetryPacket, "CLIENT", "SERVER", "Telemetry sent.");
//        sequenceNumber++;
//
//        agc::Packet ackPacket{};
//        if (agc::receivePacket(clientSocket, ackPacket) == false)
//        {
//            std::cerr << "[CLIENT] ERROR: telemetry ACK not received.\n";
//            logger.logText("Telemetry ACK not received.");
//            agc::closeSocket(clientSocket);
//            agc::cleanupSockets();
//            return 1;
//        }
//
//        logger.logPacket("RX", ackPacket, "SERVER", "CLIENT", "Telemetry ACK received.");
//
//        const agc::MessageType responseType =
//            static_cast<agc::MessageType>(ackPacket.header.messageType);
//
//        if (responseType != agc::MessageType::TELEMETRY_ACK)
//        {
//            std::cerr << "[CLIENT] ERROR: unexpected response instead of TELEMETRY_ACK.\n";
//            logger.logText("Unexpected response instead of TELEMETRY_ACK.");
//            agc::closeSocket(clientSocket);
//            agc::cleanupSockets();
//            return 1;
//        }
//
//        std::cout << "[CLIENT] Telemetry ACK received.\n";
//        std::this_thread::sleep_for(std::chrono::seconds(2));
//    }
//
//    {
//        const std::string disconnectText = "Client disconnecting.";
//        const std::vector<std::uint8_t> payload(disconnectText.begin(), disconnectText.end());
//
//        agc::Packet disconnectPacket =
//            agc::makePacket(agc::MessageType::DISCONNECT,
//                            sequenceNumber,
//                            aircraftId,
//                            payload);
//
//        if (agc::sendPacket(clientSocket, disconnectPacket) == false)
//        {
//            std::cerr << "[CLIENT] ERROR: failed to send disconnect packet.\n";
//            logger.logText("Failed to send disconnect packet.");
//            agc::closeSocket(clientSocket);
//            agc::cleanupSockets();
//            return 1;
//        }
//
//        logger.logPacket("TX", disconnectPacket, "CLIENT", "SERVER", "Disconnect sent.");
//
//        agc::Packet finalAck{};
//        if (agc::receivePacket(clientSocket, finalAck) == true)
//        {
//            logger.logPacket("RX", finalAck, "SERVER", "CLIENT", "Disconnect ACK received.");
//        }
//    }
//
//    std::cout << "[CLIENT] Session completed successfully.\n";
//
//    agc::closeSocket(clientSocket);
//    agc::cleanupSockets();
//    return 0;
//}



//Updated code for Sprint 2

#include "common.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    agc::TelemetryData generateTelemetry(std::uint32_t sampleIndex)
    {
        agc::TelemetryData telemetry{};
        telemetry.altitudeFt = 30000.0 + static_cast<double>(sampleIndex * 100U);
        telemetry.speedKnots = 450.0 + static_cast<double>(sampleIndex * 2U);
        telemetry.headingDeg = 90.0 + static_cast<double>(sampleIndex * 1U);
        telemetry.fuelPercent = 80.0 - static_cast<double>(sampleIndex * 1U);
        return telemetry;
    }

    bool connectWithRetries(SOCKET clientSocket, const sockaddr_in& serverAddress)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RECONNECT_ATTEMPTS; ++attempt)
        {
            std::cout << "[CLIENT] Connection attempt " << attempt << "...\n";

            if (connect(clientSocket,
                reinterpret_cast<const sockaddr*>(&serverAddress),
                static_cast<int>(sizeof(serverAddress))) != SOCKET_ERROR)
            {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return false;
    }

    bool sendAndLog(SOCKET socketHandle,
        agc::Logger& logger,
        const agc::Packet& packet,
        const std::string& note)
    {
        const bool ok = agc::sendPacket(socketHandle, packet);

        if (ok == true)
        {
            logger.logPacket("TX", packet, "CLIENT", "SERVER", note);
        }
        else
        {
            logger.logError("TX_FAIL", note);
        }

        return ok;
    }

    bool receiveAndLog(SOCKET socketHandle,
        agc::Logger& logger,
        agc::Packet& packet,
        const std::string& note)
    {
        const bool ok = agc::receivePacket(socketHandle, packet);

        if (ok == true)
        {
            logger.logPacket("RX", packet, "SERVER", "CLIENT", note);
        }
        else
        {
            logger.logError("RX_FAIL", note);
        }

        return ok;
    }

    bool receiveExpectedPacketWithRetransmission(SOCKET socketHandle,
        agc::Logger& logger,
        agc::MessageType expectedType,
        agc::Packet& packet,
        const std::string& context)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (agc::waitForReadable(socketHandle, agc::SOCKET_TIMEOUT_MS) == false)
            {
                logger.logError("RETRANSMIT_TIMEOUT",
                    context + " timed out on attempt " + std::to_string(attempt));
                continue;
            }

            if (receiveAndLog(socketHandle, logger, packet,
                context + " received on attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("RETRANSMIT_RX",
                    context + " receive failed on attempt " + std::to_string(attempt));
                continue;
            }

            if (static_cast<agc::MessageType>(packet.header.messageType) == expectedType)
            {
                return true;
            }

            logger.logError("RETRANSMIT_TYPE",
                context + " wrong packet type on attempt " + std::to_string(attempt));
        }

        return false;
    }

    bool sendTelemetryWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        const agc::Packet& telemetryPacket,
        std::uint32_t& sequenceNumber)
    {
        agc::Packet ackPacket{};

        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (sendAndLog(clientSocket, logger, telemetryPacket,
                "Telemetry sent. Attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("TELEMETRY_SEND",
                    "Telemetry send failed on attempt " + std::to_string(attempt));
                continue;
            }

            if (receiveExpectedPacketWithRetransmission(clientSocket,
                logger,
                agc::MessageType::TELEMETRY_ACK,
                ackPacket,
                "Telemetry ACK") == true)
            {
                sequenceNumber++;
                std::cout << "[CLIENT] Telemetry ACK received.\n";
                return true;
            }

            logger.logError("TELEMETRY_ACK",
                "Telemetry ACK not received on attempt " + std::to_string(attempt));
        }

        return false;
    }

    bool handleCommand(SOCKET clientSocket,
        agc::Logger& logger,
        const std::string& aircraftId,
        const agc::Packet& packet,
        std::uint32_t& sequenceNumber)
    {
        const std::string commandText = agc::payloadToString(packet.payload);
        std::cout << "[CLIENT] Command received: " << commandText << "\n";

        const agc::Packet ackPacket =
            agc::makePacket(agc::MessageType::COMMAND_ACK,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("COMMAND_EXECUTED:" + commandText));

        const bool ok = sendAndLog(clientSocket, logger, ackPacket, "Command ACK sent.");
        sequenceNumber++;
        return ok;
    }

    bool handleDataRequest(SOCKET clientSocket,
        agc::Logger& logger,
        const std::string& aircraftId,
        const agc::Packet& packet,
        std::uint32_t& sequenceNumber)
    {
        const std::string requestText = agc::payloadToString(packet.payload);
        std::cout << "[CLIENT] Data request received: " << requestText << "\n";

        const std::string statusText =
            "ENGINE_TEMP=91.5;OIL_PRESSURE=52.2;CABIN_PRESSURE=11.4;SYSTEM=NOMINAL";

        const agc::Packet responsePacket =
            agc::makePacket(agc::MessageType::STATUS_RESPONSE,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(statusText));

        const bool ok = sendAndLog(clientSocket, logger, responsePacket, "Status response sent.");
        sequenceNumber++;
        return ok;
    }

    bool receiveLargeTransfer(SOCKET clientSocket,
        agc::Logger& logger,
        const std::string& aircraftId,
        const agc::Packet& startPacket,
        std::uint32_t& sequenceNumber)
    {
        const std::string startText = agc::payloadToString(startPacket.payload);
        std::cout << "[CLIENT] Large transfer start: " << startText << "\n";

        std::size_t expectedSize = 0U;
        std::uint32_t expectedChunks = 0U;
        std::uint32_t expectedChecksum = 0U;

        {
            const std::size_t sizePos = startText.find("size=");
            const std::size_t chunksPos = startText.find(";chunks=");
            const std::size_t checksumPos = startText.find(";checksum=");

            if ((sizePos == std::string::npos) ||
                (chunksPos == std::string::npos) ||
                (checksumPos == std::string::npos))
            {
                logger.logError("TRANSFER_META", "Invalid transfer metadata.");
                return false;
            }

            expectedSize = static_cast<std::size_t>(
                std::stoull(startText.substr(sizePos + 5U, chunksPos - (sizePos + 5U))));

            expectedChunks = static_cast<std::uint32_t>(
                std::stoul(startText.substr(chunksPos + 8U, checksumPos - (chunksPos + 8U))));

            expectedChecksum = static_cast<std::uint32_t>(
                std::stoul(startText.substr(checksumPos + 10U)));
        }

        std::vector<std::uint8_t> fileBuffer{};
        fileBuffer.reserve(expectedSize);

        for (std::uint32_t chunkCount = 0U; chunkCount < expectedChunks; ++chunkCount)
        {
            agc::Packet chunkPacket{};

            if (receiveAndLog(clientSocket, logger, chunkPacket, "Large transfer chunk received.") == false)
            {
                return false;
            }

            if (static_cast<agc::MessageType>(chunkPacket.header.messageType) != agc::MessageType::LARGE_DATA_CHUNK)
            {
                logger.logError("TRANSFER_TYPE", "Expected LARGE_DATA_CHUNK.");
                return false;
            }

            fileBuffer.insert(fileBuffer.end(), chunkPacket.payload.begin(), chunkPacket.payload.end());
        }

        agc::Packet endPacket{};
        if (receiveAndLog(clientSocket, logger, endPacket, "Large transfer end received.") == false)
        {
            return false;
        }

        if (static_cast<agc::MessageType>(endPacket.header.messageType) != agc::MessageType::LARGE_DATA_END)
        {
            logger.logError("TRANSFER_END", "Expected LARGE_DATA_END.");
            return false;
        }

        const std::uint32_t actualChecksum = agc::computeChecksum(fileBuffer);
        const bool sizeOk = (fileBuffer.size() == expectedSize);
        const bool checksumOk = (actualChecksum == expectedChecksum);

        std::ofstream outputFile("received_diagnostic_payload.bin", std::ios::binary);
        if (outputFile.is_open())
        {
            outputFile.write(reinterpret_cast<const char*>(fileBuffer.data()),
                static_cast<std::streamsize>(fileBuffer.size()));
            outputFile.close();
        }

        const std::string statusText =
            std::string("TRANSFER_OK=") +
            ((sizeOk && checksumOk) ? "TRUE" : "FALSE") +
            ";SIZE=" + std::to_string(fileBuffer.size()) +
            ";CHECKSUM=" + std::to_string(actualChecksum);

        const agc::Packet statusPacket =
            agc::makePacket(agc::MessageType::STATUS_RESPONSE,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(statusText));

        const bool ok = sendAndLog(clientSocket, logger, statusPacket, "Transfer completion response sent.");
        sequenceNumber++;

        std::cout << "[CLIENT] Large transfer complete. File saved as received_diagnostic_payload.bin\n";
        return ok && sizeOk && checksumOk;
    }

    bool handleOptionalServerMessage(SOCKET clientSocket,
        agc::Logger& logger,
        const std::string& aircraftId,
        std::uint32_t& sequenceNumber,
        bool& largeTransferCompleted)
    {
        if (agc::waitForReadable(clientSocket, agc::OPTIONAL_WAIT_MS) == false)
        {
            return true;
        }

        agc::Packet packet{};
        if (receiveAndLog(clientSocket, logger, packet, "Optional server message received.") == false)
        {
            return false;
        }

        const agc::MessageType type =
            static_cast<agc::MessageType>(packet.header.messageType);

        if (type == agc::MessageType::COMMAND)
        {
            return handleCommand(clientSocket, logger, aircraftId, packet, sequenceNumber);
        }

        if (type == agc::MessageType::DATA_REQUEST)
        {
            return handleDataRequest(clientSocket, logger, aircraftId, packet, sequenceNumber);
        }

        if (type == agc::MessageType::LARGE_DATA_START)
        {
            const bool ok = receiveLargeTransfer(clientSocket,
                logger,
                aircraftId,
                packet,
                sequenceNumber);

            largeTransferCompleted = ok;
            return ok;
        }

        return true;
    }
}

int main()
{
    if (agc::initializeSockets() == false)
    {
        std::cerr << "[CLIENT] ERROR: WSAStartup failed.\n";
        return 1;
    }

    agc::Logger logger("client_log.csv");

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "[CLIENT] ERROR: socket creation failed.\n";
        agc::cleanupSockets();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(agc::SERVER_PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr) != 1)
    {
        std::cerr << "[CLIENT] ERROR: invalid server address.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    (void)agc::setSocketTimeouts(clientSocket, agc::SOCKET_TIMEOUT_MS);

    std::cout << "[CLIENT] Connecting to server...\n";
    if (connectWithRetries(clientSocket, serverAddress) == false)
    {
        std::cerr << "[CLIENT] ERROR: all connection attempts failed.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    const std::string aircraftId = "ACFT001";
    std::uint32_t sequenceNumber = 1U;

    /*{
        const agc::Packet connectPacket =
            agc::makePacket(agc::MessageType::CONNECT_REQUEST,
                            sequenceNumber,
                            aircraftId,
                            agc::stringToPayload("Aircraft requesting connection."));

        if (sendAndLog(clientSocket, logger, connectPacket, "Connect request sent.") == false)
        {
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        sequenceNumber++;
    }*/

    /*agc::Packet responsePacket{};
    if (receiveAndLog(clientSocket, logger, responsePacket, "Handshake response received.") == false)
    {
        std::cerr << "[CLIENT] ERROR: no response from server.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    if (static_cast<agc::MessageType>(responsePacket.header.messageType) != agc::MessageType::CONNECT_ACK)
    {
        std::cerr << "[CLIENT] ERROR: connection not acknowledged by server.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }*/

    agc::Packet responsePacket{};
    bool connectOk = false;

    for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
    {
        const agc::Packet connectPacket =
            agc::makePacket(agc::MessageType::CONNECT_REQUEST,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("Aircraft requesting connection."));

        if (sendAndLog(clientSocket, logger, connectPacket,
            "Connect request sent. Attempt " + std::to_string(attempt)) == false)
        {
            logger.logError("CONNECT_SEND",
                "Connect request send failed on attempt " + std::to_string(attempt));
            continue;
        }

        if (receiveExpectedPacketWithRetransmission(clientSocket,
            logger,
            agc::MessageType::CONNECT_ACK,
            responsePacket,
            "Handshake response") == true)
        {
            connectOk = true;
            sequenceNumber++;
            break;
        }

        logger.logError("CONNECT_ACK",
            "Connect ACK not received on attempt " + std::to_string(attempt));
    }

    if (connectOk == false)
    {
        std::cerr << "[CLIENT] ERROR: connection not acknowledged by server.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    std::cout << "[CLIENT] Connection verified by server.\n";

    bool largeTransferCompleted = false;

    for (std::uint32_t index = 0U; index < 5U; ++index)
    {
        const agc::TelemetryData telemetry = generateTelemetry(index);
        const std::vector<std::uint8_t> payload = agc::telemetryToPayload(telemetry);

        const agc::Packet telemetryPacket =
            agc::makePacket(agc::MessageType::TELEMETRY,
                sequenceNumber,
                aircraftId,
                payload);

        std::cout << "[CLIENT] Sending telemetry packet #" << sequenceNumber << "\n";

        /*if (sendAndLog(clientSocket, logger, telemetryPacket, "Telemetry sent.") == false)
        {
            std::cerr << "[CLIENT] ERROR: telemetry send failed.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        sequenceNumber++;

        agc::Packet ackPacket{};
        if (receiveAndLog(clientSocket, logger, ackPacket, "Telemetry ACK received.") == false)
        {
            std::cerr << "[CLIENT] ERROR: telemetry ACK not received.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        const agc::MessageType responseType =
            static_cast<agc::MessageType>(ackPacket.header.messageType);

        if (responseType != agc::MessageType::TELEMETRY_ACK)
        {
            std::cerr << "[CLIENT] ERROR: unexpected response instead of TELEMETRY_ACK.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        std::cout << "[CLIENT] Telemetry ACK received.\n";*/

        if (sendTelemetryWithRetransmission(clientSocket,
            logger,
            telemetryPacket,
            sequenceNumber) == false)
        {
            std::cerr << "[CLIENT] ERROR: telemetry retransmission attempts exhausted.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        if (handleOptionalServerMessage(clientSocket,
            logger,
            aircraftId,
            sequenceNumber,
            largeTransferCompleted) == false)
        {
            std::cerr << "[CLIENT] ERROR: failed while handling optional server message.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    /*
      After the telemetry loop ends, the server may still initiate the large
      diagnostic transfer. Give it a small post-telemetry window.
    */
    for (std::uint32_t waitCycle = 0U; waitCycle < 10U; ++waitCycle)
    {
        if (largeTransferCompleted == true)
        {
            break;
        }

        if (handleOptionalServerMessage(clientSocket,
            logger,
            aircraftId,
            sequenceNumber,
            largeTransferCompleted) == false)
        {
            std::cerr << "[CLIENT] ERROR: failed during post-telemetry handling.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    {
        const agc::Packet disconnectPacket =
            agc::makePacket(agc::MessageType::DISCONNECT,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("Client disconnecting."));

        if (sendAndLog(clientSocket, logger, disconnectPacket, "Disconnect sent.") == false)
        {
            std::cerr << "[CLIENT] ERROR: failed to send disconnect packet.\n";
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        agc::Packet finalAck{};
        if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == true)
        {
            (void)receiveAndLog(clientSocket, logger, finalAck, "Disconnect ACK received.");
        }
    }

    std::cout << "[CLIENT] Session completed successfully.\n";

    agc::closeSocket(clientSocket);
    agc::cleanupSockets();
    return 0;
}