/*
This file serves as the Aircraft Client for Sprint 1. Establishes TCP connection, sends connect request, transmits telemetry periodically, validates server responses, logs activity, and disconnects gracefully.

Critical Sections:
1. Initial handshake: The client must wait for verification before sending telemetry.
2. Telemetry send/ack cycle: Each sent packet must be confirmed before the next step.
3. Connection loss handling: Any send/receive failure is treated as a controlled communication failure.
*/

#include "common.hpp"

#include <chrono>
#include <iostream>
#include <thread>

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

    std::cout << "[CLIENT] Connecting to server...\n";
    if (connect(clientSocket,
                reinterpret_cast<sockaddr*>(&serverAddress),
                static_cast<int>(sizeof(serverAddress))) == SOCKET_ERROR)
    {
        std::cerr << "[CLIENT] ERROR: connect failed.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    const std::string aircraftId = "ACFT001";
    std::uint32_t sequenceNumber = 1U;

    {
        const std::string connectText = "Aircraft requesting connection.";
        const std::vector<std::uint8_t> payload(connectText.begin(), connectText.end());

        agc::Packet connectPacket =
            agc::makePacket(agc::MessageType::CONNECT_REQUEST,
                            sequenceNumber,
                            aircraftId,
                            payload);

        if (agc::sendPacket(clientSocket, connectPacket) == false)
        {
            std::cerr << "[CLIENT] ERROR: failed to send connect request.\n";
            logger.logText("Failed to send connect request.");
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        logger.logPacket("TX", connectPacket, "CLIENT", "SERVER", "Connect request sent.");
        sequenceNumber++;
    }

    agc::Packet responsePacket{};
    if (agc::receivePacket(clientSocket, responsePacket) == false)
    {
        std::cerr << "[CLIENT] ERROR: no response from server after connect request.\n";
        logger.logText("No response from server after connect request.");
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    logger.logPacket("RX", responsePacket, "SERVER", "CLIENT", "Handshake response received.");

    if (static_cast<agc::MessageType>(responsePacket.header.messageType) !=
        agc::MessageType::CONNECT_ACK)
    {
        std::cerr << "[CLIENT] ERROR: connection not acknowledged by server.\n";
        logger.logText("Connection not acknowledged by server.");
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    std::cout << "[CLIENT] Connection verified by server.\n";

    for (std::uint32_t index = 0U; index < 5U; ++index)
    {
        const agc::TelemetryData telemetry = generateTelemetry(index);
        const std::vector<std::uint8_t> payload = agc::telemetryToPayload(telemetry);

        agc::Packet telemetryPacket =
            agc::makePacket(agc::MessageType::TELEMETRY,
                            sequenceNumber,
                            aircraftId,
                            payload);

        std::cout << "[CLIENT] Sending telemetry packet #" << sequenceNumber << "\n";

        if (agc::sendPacket(clientSocket, telemetryPacket) == false)
        {
            std::cerr << "[CLIENT] ERROR: telemetry send failed. Connection may be lost.\n";
            logger.logText("Telemetry send failed.");
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        logger.logPacket("TX", telemetryPacket, "CLIENT", "SERVER", "Telemetry sent.");
        sequenceNumber++;

        agc::Packet ackPacket{};
        if (agc::receivePacket(clientSocket, ackPacket) == false)
        {
            std::cerr << "[CLIENT] ERROR: telemetry ACK not received.\n";
            logger.logText("Telemetry ACK not received.");
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        logger.logPacket("RX", ackPacket, "SERVER", "CLIENT", "Telemetry ACK received.");

        const agc::MessageType responseType =
            static_cast<agc::MessageType>(ackPacket.header.messageType);

        if (responseType != agc::MessageType::TELEMETRY_ACK)
        {
            std::cerr << "[CLIENT] ERROR: unexpected response instead of TELEMETRY_ACK.\n";
            logger.logText("Unexpected response instead of TELEMETRY_ACK.");
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        std::cout << "[CLIENT] Telemetry ACK received.\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    {
        const std::string disconnectText = "Client disconnecting.";
        const std::vector<std::uint8_t> payload(disconnectText.begin(), disconnectText.end());

        agc::Packet disconnectPacket =
            agc::makePacket(agc::MessageType::DISCONNECT,
                            sequenceNumber,
                            aircraftId,
                            payload);

        if (agc::sendPacket(clientSocket, disconnectPacket) == false)
        {
            std::cerr << "[CLIENT] ERROR: failed to send disconnect packet.\n";
            logger.logText("Failed to send disconnect packet.");
            agc::closeSocket(clientSocket);
            agc::cleanupSockets();
            return 1;
        }

        logger.logPacket("TX", disconnectPacket, "CLIENT", "SERVER", "Disconnect sent.");

        agc::Packet finalAck{};
        if (agc::receivePacket(clientSocket, finalAck) == true)
        {
            logger.logPacket("RX", finalAck, "SERVER", "CLIENT", "Disconnect ACK received.");
        }
    }

    std::cout << "[CLIENT] Session completed successfully.\n";

    agc::closeSocket(clientSocket);
    agc::cleanupSockets();
    return 0;
}