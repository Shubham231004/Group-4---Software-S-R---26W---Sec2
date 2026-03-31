/*
This file serves as the Ground Control Server for Sprint 1. Handles listening, client verification, telemetry reception, acknowledgement, logging, and safe disconnect.

Critical Sections:
1. Verification stage: The server must reject invalid first packets.
2. Active telemetry stage: Every telemetry packet is validated before processing.
3. Error handling: On any invalid input, the server moves to a controlled error path.
*/

#include "common.hpp"

#include <iostream>
#include <string>

namespace
{
    void printState(agc::ServerState state)
    {
        switch (state)
        {
            case agc::ServerState::STARTUP:
                std::cout << "[SERVER] State: STARTUP\n";
                break;
            case agc::ServerState::IDLE_LISTENING:
                std::cout << "[SERVER] State: IDLE_LISTENING\n";
                break;
            case agc::ServerState::VERIFICATION:
                std::cout << "[SERVER] State: VERIFICATION\n";
                break;
            case agc::ServerState::ACTIVE:
                std::cout << "[SERVER] State: ACTIVE\n";
                break;
            case agc::ServerState::ERROR_STATE:
                std::cout << "[SERVER] State: ERROR_STATE\n";
                break;
            case agc::ServerState::SHUTDOWN:
                std::cout << "[SERVER] State: SHUTDOWN\n";
                break;
            default:
                std::cout << "[SERVER] State: UNKNOWN\n";
                break;
        }
    }
}

int main()
{
    int exitCode = 0;
    agc::ServerState state = agc::ServerState::STARTUP;
    printState(state);

    agc::Logger logger("server_log.csv");

    if (agc::initializeSockets() == false)
    {
        std::cerr << "[SERVER] ERROR: WSAStartup failed.\n";
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "[SERVER] ERROR: listen socket creation failed.\n";
        agc::cleanupSockets();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(agc::SERVER_PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenSocket,
             reinterpret_cast<sockaddr*>(&serverAddress),
             static_cast<int>(sizeof(serverAddress))) == SOCKET_ERROR)
    {
        std::cerr << "[SERVER] ERROR: bind failed.\n";
        agc::closeSocket(listenSocket);
        agc::cleanupSockets();
        return 1;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR)
    {
        std::cerr << "[SERVER] ERROR: listen failed.\n";
        agc::closeSocket(listenSocket);
        agc::cleanupSockets();
        return 1;
    }

    state = agc::ServerState::IDLE_LISTENING;
    printState(state);
    logger.logText("Server started and listening on port 5050.");

    std::cout << "[SERVER] Waiting for client connection...\n";

    sockaddr_in clientAddress{};
    int clientAddressLength = static_cast<int>(sizeof(clientAddress));
    SOCKET clientSocket = accept(listenSocket,
                                 reinterpret_cast<sockaddr*>(&clientAddress),
                                 &clientAddressLength);

    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "[SERVER] ERROR: accept failed.\n";
        agc::closeSocket(listenSocket);
        agc::cleanupSockets();
        return 1;
    }

    state = agc::ServerState::VERIFICATION;
    printState(state);

    agc::Packet firstPacket{};
    bool sessionActive = false;
    std::uint32_t responseSequence = 1U;

    if (agc::receivePacket(clientSocket, firstPacket) == false)
    {
        std::cerr << "[SERVER] ERROR: failed to receive first packet.\n";
        logger.logText("Failed to receive first packet.");
        state = agc::ServerState::ERROR_STATE;
    }
    else
    {
        logger.logPacket("RX", firstPacket, "CLIENT", "SERVER", "First packet received.");

        const bool typeValid =
            (static_cast<agc::MessageType>(firstPacket.header.messageType) ==
             agc::MessageType::CONNECT_REQUEST);

        const bool idValid =
            agc::validateAircraftId(firstPacket.header.aircraftId);

        const bool timestampValid =
            agc::validateTimestamp(firstPacket.header.timestampMs, agc::getCurrentTimeMs());

        const bool payloadSizeValid =
            agc::validatePayloadSize(firstPacket.header.payloadSize);

        if ((typeValid == true) &&
            (idValid == true) &&
            (timestampValid == true) &&
            (payloadSizeValid == true))
        {
            const std::string aircraftId =
                agc::safeAircraftIdToString(firstPacket.header.aircraftId);

            const std::string ackText = "Connection verified.";
            const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());

            agc::Packet ackPacket =
                agc::makePacket(agc::MessageType::CONNECT_ACK,
                                responseSequence,
                                aircraftId,
                                ackPayload);

            if (agc::sendPacket(clientSocket, ackPacket) == true)
            {
                logger.logPacket("TX", ackPacket, "SERVER", "CLIENT", "Connection ACK sent.");
                std::cout << "[SERVER] Client verified successfully.\n";
                sessionActive = true;
                state = agc::ServerState::ACTIVE;
                printState(state);
                responseSequence++;
            }
            else
            {
                std::cerr << "[SERVER] ERROR: failed to send connection ACK.\n";
                state = agc::ServerState::ERROR_STATE;
            }
        }
        else
        {
            const std::string aircraftId =
                agc::safeAircraftIdToString(firstPacket.header.aircraftId);

            agc::Packet errorPacket =
                agc::makeErrorPacket(responseSequence, aircraftId, "Verification failed.");

            (void)agc::sendPacket(clientSocket, errorPacket);
            logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Verification failed.");
            std::cerr << "[SERVER] ERROR: client verification failed.\n";
            state = agc::ServerState::ERROR_STATE;
        }
    }

    while (sessionActive == true)
    {
        agc::Packet incomingPacket{};

        if (agc::receivePacket(clientSocket, incomingPacket) == false)
        {
            std::cerr << "[SERVER] ERROR: receive failed or client disconnected unexpectedly.\n";
            logger.logText("Receive failed during active session.");
            state = agc::ServerState::ERROR_STATE;
            break;
        }

        logger.logPacket("RX", incomingPacket, "CLIENT", "SERVER", "Packet received during active session.");

        const agc::MessageType messageType =
            static_cast<agc::MessageType>(incomingPacket.header.messageType);

        if (messageType == agc::MessageType::TELEMETRY)
        {
            const bool idValid =
                agc::validateAircraftId(incomingPacket.header.aircraftId);

            const bool timestampValid =
                agc::validateTimestamp(incomingPacket.header.timestampMs, agc::getCurrentTimeMs());

            const bool payloadSizeValid =
                agc::validatePayloadSize(incomingPacket.header.payloadSize);

            agc::TelemetryData telemetry{};

            const bool telemetryValid =
                agc::payloadToTelemetry(incomingPacket.payload, telemetry);

            if ((idValid == true) &&
                (timestampValid == true) &&
                (payloadSizeValid == true) &&
                (telemetryValid == true))
            {
                std::cout << "\n[SERVER] Telemetry received\n";
                std::cout << "  Altitude (ft): " << telemetry.altitudeFt << "\n";
                std::cout << "  Speed (knots): " << telemetry.speedKnots << "\n";
                std::cout << "  Heading (deg): " << telemetry.headingDeg << "\n";
                std::cout << "  Fuel (%): " << telemetry.fuelPercent << "\n";

                const std::string aircraftId =
                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

                const std::string ackText = "Telemetry accepted.";
                const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());

                agc::Packet ackPacket =
                    agc::makePacket(agc::MessageType::TELEMETRY_ACK,
                                    responseSequence,
                                    aircraftId,
                                    ackPayload);

                if (agc::sendPacket(clientSocket, ackPacket) == true)
                {
                    logger.logPacket("TX", ackPacket, "SERVER", "CLIENT", "Telemetry ACK sent.");
                    responseSequence++;
                }
                else
                {
                    std::cerr << "[SERVER] ERROR: failed to send telemetry ACK.\n";
                    logger.logText("Failed to send telemetry ACK.");
                    state = agc::ServerState::ERROR_STATE;
                    break;
                }
            }
            else
            {
                const std::string aircraftId =
                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

                agc::Packet errorPacket =
                    agc::makeErrorPacket(responseSequence, aircraftId, "Invalid telemetry packet.");

                (void)agc::sendPacket(clientSocket, errorPacket);
                logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Invalid telemetry packet.");
                state = agc::ServerState::ERROR_STATE;
                break;
            }
        }
        else if (messageType == agc::MessageType::DISCONNECT)
        {
            std::cout << "[SERVER] Disconnect request received.\n";
            logger.logText("Disconnect request received.");

            const std::string aircraftId =
                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

            const std::string ackText = "Session closed.";
            const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());

            agc::Packet disconnectAck =
                agc::makePacket(agc::MessageType::DISCONNECT,
                                responseSequence,
                                aircraftId,
                                ackPayload);

            (void)agc::sendPacket(clientSocket, disconnectAck);
            logger.logPacket("TX", disconnectAck, "SERVER", "CLIENT", "Disconnect ACK sent.");
            sessionActive = false;
            state = agc::ServerState::IDLE_LISTENING;
            printState(state);
        }
        else
        {
            const std::string aircraftId =
                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

            agc::Packet errorPacket =
                agc::makeErrorPacket(responseSequence, aircraftId, "Unexpected message type.");

            (void)agc::sendPacket(clientSocket, errorPacket);
            logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Unexpected message type.");
            std::cerr << "[SERVER] ERROR: unexpected message type received.\n";
            state = agc::ServerState::ERROR_STATE;
            break;
        }
    }

    if (state == agc::ServerState::ERROR_STATE)
    {
        printState(state);
        logger.logText("Server entered ERROR_STATE.");
    }

    agc::closeSocket(clientSocket);
    agc::closeSocket(listenSocket);

    state = agc::ServerState::SHUTDOWN;
    printState(state);
    logger.logText("Server shutdown complete.");

    agc::cleanupSockets();
    return exitCode;
}