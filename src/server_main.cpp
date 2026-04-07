///*
//Purpose   : Ground Control Server for Sprint 1.
//            Handles listening, client verification, telemetry reception,
//            acknowledgement, logging, and safe disconnect.
//
//Critical Sections:
//1. Verification stage:
//   The server must reject invalid first packets.
//2. Active telemetry stage:
//   Every telemetry packet is validated before processing.
//3. Error handling:
//   On any invalid input, the server moves to a controlled error path.
//*/
//
//#include "common.hpp"
//
//#include <iostream>
//#include <string>
//
//namespace
//{
//    void printState(agc::ServerState state)
//    {
//        switch (state)
//        {
//            case agc::ServerState::STARTUP:
//                std::cout << "[SERVER] State: STARTUP\n";
//                break;
//            case agc::ServerState::IDLE_LISTENING:
//                std::cout << "[SERVER] State: IDLE_LISTENING\n";
//                break;
//            case agc::ServerState::VERIFICATION:
//                std::cout << "[SERVER] State: VERIFICATION\n";
//                break;
//            case agc::ServerState::ACTIVE:
//                std::cout << "[SERVER] State: ACTIVE\n";
//                break;
//            case agc::ServerState::ERROR_STATE:
//                std::cout << "[SERVER] State: ERROR_STATE\n";
//                break;
//            case agc::ServerState::SHUTDOWN:
//                std::cout << "[SERVER] State: SHUTDOWN\n";
//                break;
//            default:
//                std::cout << "[SERVER] State: UNKNOWN\n";
//                break;
//        }
//    }
//}
//
//int main()
//{
//    int exitCode = 0;
//    agc::ServerState state = agc::ServerState::STARTUP;
//    printState(state);
//
//    agc::Logger logger("server_log.csv");
//
//    if (agc::initializeSockets() == false)
//    {
//        std::cerr << "[SERVER] ERROR: WSAStartup failed.\n";
//        return 1;
//    }
//
//    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
//    if (listenSocket == INVALID_SOCKET)
//    {
//        std::cerr << "[SERVER] ERROR: listen socket creation failed.\n";
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    sockaddr_in serverAddress{};
//    serverAddress.sin_family = AF_INET;
//    serverAddress.sin_port = htons(agc::SERVER_PORT);
//    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
//
//    if (bind(listenSocket,
//             reinterpret_cast<sockaddr*>(&serverAddress),
//             static_cast<int>(sizeof(serverAddress))) == SOCKET_ERROR)
//    {
//        std::cerr << "[SERVER] ERROR: bind failed.\n";
//        agc::closeSocket(listenSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    if (listen(listenSocket, 1) == SOCKET_ERROR)
//    {
//        std::cerr << "[SERVER] ERROR: listen failed.\n";
//        agc::closeSocket(listenSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    state = agc::ServerState::IDLE_LISTENING;
//    printState(state);
//    logger.logText("Server started and listening on port 5050.");
//
//    std::cout << "[SERVER] Waiting for client connection...\n";
//
//    sockaddr_in clientAddress{};
//    int clientAddressLength = static_cast<int>(sizeof(clientAddress));
//    SOCKET clientSocket = accept(listenSocket,
//                                 reinterpret_cast<sockaddr*>(&clientAddress),
//                                 &clientAddressLength);
//
//    if (clientSocket == INVALID_SOCKET)
//    {
//        std::cerr << "[SERVER] ERROR: accept failed.\n";
//        agc::closeSocket(listenSocket);
//        agc::cleanupSockets();
//        return 1;
//    }
//
//    state = agc::ServerState::VERIFICATION;
//    printState(state);
//
//    agc::Packet firstPacket{};
//    bool sessionActive = false;
//    std::uint32_t responseSequence = 1U;
//
//    if (agc::receivePacket(clientSocket, firstPacket) == false)
//    {
//        std::cerr << "[SERVER] ERROR: failed to receive first packet.\n";
//        logger.logText("Failed to receive first packet.");
//        state = agc::ServerState::ERROR_STATE;
//    }
//    else
//    {
//        logger.logPacket("RX", firstPacket, "CLIENT", "SERVER", "First packet received.");
//
//        const bool typeValid =
//            (static_cast<agc::MessageType>(firstPacket.header.messageType) ==
//             agc::MessageType::CONNECT_REQUEST);
//
//        const bool idValid =
//            agc::validateAircraftId(firstPacket.header.aircraftId);
//
//        const bool timestampValid =
//            agc::validateTimestamp(firstPacket.header.timestampMs, agc::getCurrentTimeMs());
//
//        const bool payloadSizeValid =
//            agc::validatePayloadSize(firstPacket.header.payloadSize);
//
//        if ((typeValid == true) &&
//            (idValid == true) &&
//            (timestampValid == true) &&
//            (payloadSizeValid == true))
//        {
//            const std::string aircraftId =
//                agc::safeAircraftIdToString(firstPacket.header.aircraftId);
//
//            const std::string ackText = "Connection verified.";
//            const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());
//
//            agc::Packet ackPacket =
//                agc::makePacket(agc::MessageType::CONNECT_ACK,
//                                responseSequence,
//                                aircraftId,
//                                ackPayload);
//
//            if (agc::sendPacket(clientSocket, ackPacket) == true)
//            {
//                logger.logPacket("TX", ackPacket, "SERVER", "CLIENT", "Connection ACK sent.");
//                std::cout << "[SERVER] Client verified successfully.\n";
//                sessionActive = true;
//                state = agc::ServerState::ACTIVE;
//                printState(state);
//                responseSequence++;
//            }
//            else
//            {
//                std::cerr << "[SERVER] ERROR: failed to send connection ACK.\n";
//                state = agc::ServerState::ERROR_STATE;
//            }
//        }
//        else
//        {
//            const std::string aircraftId =
//                agc::safeAircraftIdToString(firstPacket.header.aircraftId);
//
//            agc::Packet errorPacket =
//                agc::makeErrorPacket(responseSequence, aircraftId, "Verification failed.");
//
//            (void)agc::sendPacket(clientSocket, errorPacket);
//            logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Verification failed.");
//            std::cerr << "[SERVER] ERROR: client verification failed.\n";
//            state = agc::ServerState::ERROR_STATE;
//        }
//    }
//
//    while (sessionActive == true)
//    {
//        agc::Packet incomingPacket{};
//
//        if (agc::receivePacket(clientSocket, incomingPacket) == false)
//        {
//            std::cerr << "[SERVER] ERROR: receive failed or client disconnected unexpectedly.\n";
//            logger.logText("Receive failed during active session.");
//            state = agc::ServerState::ERROR_STATE;
//            break;
//        }
//
//        logger.logPacket("RX", incomingPacket, "CLIENT", "SERVER", "Packet received during active session.");
//
//        const agc::MessageType messageType =
//            static_cast<agc::MessageType>(incomingPacket.header.messageType);
//
//        if (messageType == agc::MessageType::TELEMETRY)
//        {
//            const bool idValid =
//                agc::validateAircraftId(incomingPacket.header.aircraftId);
//
//            const bool timestampValid =
//                agc::validateTimestamp(incomingPacket.header.timestampMs, agc::getCurrentTimeMs());
//
//            const bool payloadSizeValid =
//                agc::validatePayloadSize(incomingPacket.header.payloadSize);
//
//            agc::TelemetryData telemetry{};
//
//            const bool telemetryValid =
//                agc::payloadToTelemetry(incomingPacket.payload, telemetry);
//
//            if ((idValid == true) &&
//                (timestampValid == true) &&
//                (payloadSizeValid == true) &&
//                (telemetryValid == true))
//            {
//                std::cout << "\n[SERVER] Telemetry received\n";
//                std::cout << "  Altitude (ft): " << telemetry.altitudeFt << "\n";
//                std::cout << "  Speed (knots): " << telemetry.speedKnots << "\n";
//                std::cout << "  Heading (deg): " << telemetry.headingDeg << "\n";
//                std::cout << "  Fuel (%): " << telemetry.fuelPercent << "\n";
//
//                const std::string aircraftId =
//                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);
//
//                const std::string ackText = "Telemetry accepted.";
//                const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());
//
//                agc::Packet ackPacket =
//                    agc::makePacket(agc::MessageType::TELEMETRY_ACK,
//                                    responseSequence,
//                                    aircraftId,
//                                    ackPayload);
//
//                if (agc::sendPacket(clientSocket, ackPacket) == true)
//                {
//                    logger.logPacket("TX", ackPacket, "SERVER", "CLIENT", "Telemetry ACK sent.");
//                    responseSequence++;
//                }
//                else
//                {
//                    std::cerr << "[SERVER] ERROR: failed to send telemetry ACK.\n";
//                    logger.logText("Failed to send telemetry ACK.");
//                    state = agc::ServerState::ERROR_STATE;
//                    break;
//                }
//            }
//            else
//            {
//                const std::string aircraftId =
//                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);
//
//                agc::Packet errorPacket =
//                    agc::makeErrorPacket(responseSequence, aircraftId, "Invalid telemetry packet.");
//
//                (void)agc::sendPacket(clientSocket, errorPacket);
//                logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Invalid telemetry packet.");
//                state = agc::ServerState::ERROR_STATE;
//                break;
//            }
//        }
//        else if (messageType == agc::MessageType::DISCONNECT)
//        {
//            std::cout << "[SERVER] Disconnect request received.\n";
//            logger.logText("Disconnect request received.");
//
//            const std::string aircraftId =
//                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);
//
//            const std::string ackText = "Session closed.";
//            const std::vector<std::uint8_t> ackPayload(ackText.begin(), ackText.end());
//
//            agc::Packet disconnectAck =
//                agc::makePacket(agc::MessageType::DISCONNECT,
//                                responseSequence,
//                                aircraftId,
//                                ackPayload);
//
//            (void)agc::sendPacket(clientSocket, disconnectAck);
//            logger.logPacket("TX", disconnectAck, "SERVER", "CLIENT", "Disconnect ACK sent.");
//            sessionActive = false;
//            state = agc::ServerState::IDLE_LISTENING;
//            printState(state);
//        }
//        else
//        {
//            const std::string aircraftId =
//                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);
//
//            agc::Packet errorPacket =
//                agc::makeErrorPacket(responseSequence, aircraftId, "Unexpected message type.");
//
//            (void)agc::sendPacket(clientSocket, errorPacket);
//            logger.logPacket("TX", errorPacket, "SERVER", "CLIENT", "Unexpected message type.");
//            std::cerr << "[SERVER] ERROR: unexpected message type received.\n";
//            state = agc::ServerState::ERROR_STATE;
//            break;
//        }
//    }
//
//    if (state == agc::ServerState::ERROR_STATE)
//    {
//        printState(state);
//        logger.logText("Server entered ERROR_STATE.");
//    }
//
//    agc::closeSocket(clientSocket);
//    agc::closeSocket(listenSocket);
//
//    state = agc::ServerState::SHUTDOWN;
//    printState(state);
//    logger.logText("Server shutdown complete.");
//
//    agc::cleanupSockets();
//    return exitCode;
//}








// Updated code for Sprint 2

#include "common.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace
{

    bool sendPacketWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        const agc::Packet& packet,
        const std::string& successNote,
        const std::string& retryContext);

    bool receiveExpectedMessageWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        agc::MessageType expectedType,
        agc::Packet& receivedPacket,
        const std::string& retryContext);

    bool waitForCommandAck(SOCKET clientSocket,
        agc::Logger& logger);

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
        case agc::ServerState::COMMAND_PROCESSING:
            std::cout << "[SERVER] State: COMMAND_PROCESSING\n";
            break;
        case agc::ServerState::DATA_TRANSFER:
            std::cout << "[SERVER] State: DATA_TRANSFER\n";
            break;
        case agc::ServerState::DISCONNECTING:
            std::cout << "[SERVER] State: DISCONNECTING\n";
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

    bool isValidTransition(agc::ServerState currentState, agc::ServerState nextState)
    {
        bool valid = false;

        switch (currentState)
        {
        case agc::ServerState::STARTUP:
            valid = (nextState == agc::ServerState::IDLE_LISTENING) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::IDLE_LISTENING:
            valid = (nextState == agc::ServerState::VERIFICATION) ||
                (nextState == agc::ServerState::SHUTDOWN) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::VERIFICATION:
            valid = (nextState == agc::ServerState::ACTIVE) ||
                (nextState == agc::ServerState::ERROR_STATE) ||
                (nextState == agc::ServerState::IDLE_LISTENING);
            break;

        case agc::ServerState::ACTIVE:
            valid = (nextState == agc::ServerState::COMMAND_PROCESSING) ||
                (nextState == agc::ServerState::DATA_TRANSFER) ||
                (nextState == agc::ServerState::DISCONNECTING) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::COMMAND_PROCESSING:
            valid = (nextState == agc::ServerState::ACTIVE) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::DATA_TRANSFER:
            valid = (nextState == agc::ServerState::ACTIVE) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::DISCONNECTING:
            valid = (nextState == agc::ServerState::SHUTDOWN) ||
                (nextState == agc::ServerState::IDLE_LISTENING) ||
                (nextState == agc::ServerState::ERROR_STATE);
            break;

        case agc::ServerState::ERROR_STATE:
            valid = (nextState == agc::ServerState::SHUTDOWN);
            break;

        case agc::ServerState::SHUTDOWN:
            valid = false;
            break;

        default:
            valid = false;
            break;
        }

        return valid;
    }

    std::string stateToString(agc::ServerState state)
    {
        switch (state)
        {
        case agc::ServerState::STARTUP:
            return "STARTUP";
        case agc::ServerState::IDLE_LISTENING:
            return "IDLE_LISTENING";
        case agc::ServerState::VERIFICATION:
            return "VERIFICATION";
        case agc::ServerState::ACTIVE:
            return "ACTIVE";
        case agc::ServerState::COMMAND_PROCESSING:
            return "COMMAND_PROCESSING";
        case agc::ServerState::DATA_TRANSFER:
            return "DATA_TRANSFER";
        case agc::ServerState::DISCONNECTING:
            return "DISCONNECTING";
        case agc::ServerState::ERROR_STATE:
            return "ERROR_STATE";
        case agc::ServerState::SHUTDOWN:
            return "SHUTDOWN";
        default:
            return "UNKNOWN";
        }
    }

    bool transitionState(agc::ServerState& currentState,
        agc::ServerState nextState,
        agc::Logger& logger,
        const std::string& reason)
    {
        /*
          Critical Section:
          All server state changes must pass through this function so that
          invalid transitions are detected, logged, and rejected centrally.
        */
        if (isValidTransition(currentState, nextState) == false)
        {
            logger.logError("INVALID_STATE_TRANSITION",
                "Attempted transition from " +
                stateToString(currentState) +
                " to " +
                stateToString(nextState) +
                ". Reason: " + reason);
            return false;
        }

        logger.logText("State transition: " +
            stateToString(currentState) +
            " -> " +
            stateToString(nextState) +
            ". Reason: " + reason);

        currentState = nextState;
        printState(currentState);
        return true;
    }

    bool sendAndLog(SOCKET clientSocket,
        agc::Logger& logger,
        const agc::Packet& packet,
        const std::string& note)
    {
        const bool ok = agc::sendPacket(clientSocket, packet);

        if (ok == true)
        {
            logger.logPacket("TX", packet, "SERVER", "CLIENT", note);
        }
        else
        {
            logger.logError("TX_FAIL", note);
        }

        return ok;
    }

    bool receiveAndLog(SOCKET clientSocket,
        agc::Logger& logger,
        agc::Packet& packet,
        const std::string& note)
    {
        const bool ok = agc::receivePacket(clientSocket, packet);

        if (ok == true)
        {
            logger.logPacket("RX", packet, "CLIENT", "SERVER", note);
        }
        else
        {
            logger.logError("RX_FAIL", note);
        }

        return ok;
    }

    bool validateActivePacket(const agc::Packet& packet)
    {
        const bool idValid = agc::validateAircraftId(packet.header.aircraftId);
        const bool timeValid = agc::validateTimestamp(packet.header.timestampMs, agc::getCurrentTimeMs());
        const bool sizeValid = agc::validatePayloadSize(packet.header.payloadSize);

        return (idValid && timeValid && sizeValid);
    }

    /*bool waitForCommandAck(SOCKET clientSocket,
        agc::Logger& logger)
    {
        agc::Packet ackPacket{};

        if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
        {
            logger.logError("TIMEOUT", "Timed out waiting for command acknowledgement.");
            return false;
        }

        if (receiveAndLog(clientSocket, logger, ackPacket, "Command acknowledgement received.") == false)
        {
            return false;
        }

        return (static_cast<agc::MessageType>(ackPacket.header.messageType) == agc::MessageType::COMMAND_ACK);
    }*/

    bool waitForCommandAck(SOCKET clientSocket,
        agc::Logger& logger)
    {
        agc::Packet ackPacket{};

        return receiveExpectedMessageWithRetransmission(clientSocket,
            logger,
            agc::MessageType::COMMAND_ACK,
            ackPacket,
            "Command acknowledgement");
    }

    bool sendPacketWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        const agc::Packet& packet,
        const std::string& successNote,
        const std::string& retryContext)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (sendAndLog(clientSocket, logger, packet, successNote + " Attempt " + std::to_string(attempt)) == true)
            {
                return true;
            }

            logger.logError("RETRANSMIT_SEND",
                retryContext + " send failed on attempt " + std::to_string(attempt));
        }

        return false;
    }

    bool receiveExpectedMessageWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        agc::MessageType expectedType,
        agc::Packet& receivedPacket,
        const std::string& retryContext)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
            {
                logger.logError("RETRANSMIT_TIMEOUT",
                    retryContext + " timed out on attempt " + std::to_string(attempt));
                continue;
            }

            if (receiveAndLog(clientSocket, logger, receivedPacket,
                retryContext + " received on attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("RETRANSMIT_RX",
                    retryContext + " receive failed on attempt " + std::to_string(attempt));
                continue;
            }

            if (static_cast<agc::MessageType>(receivedPacket.header.messageType) == expectedType)
            {
                return true;
            }

            logger.logError("RETRANSMIT_TYPE",
                retryContext + " wrong message type on attempt " + std::to_string(attempt));
        }

        return false;
    }

    /*bool sendCommandAndAwaitAck(SOCKET clientSocket,
        agc::Logger& logger,
        agc::ServerState& state,
        std::uint32_t& sequenceNumber,
        const std::string& aircraftId,
        const std::string& commandText)
    {
        if (state != agc::ServerState::ACTIVE)
        {
            logger.logError("STATE", "Command sending attempted outside ACTIVE state.");
            return false;
        }

        if (transitionState(state,
            agc::ServerState::COMMAND_PROCESSING,
            logger,
            "Sending command to client.") == false)
        {
            return false;
        }

        const agc::Packet commandPacket =
            agc::makePacket(agc::MessageType::COMMAND,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(commandText));

        if (sendAndLog(clientSocket, logger, commandPacket, "Command sent to client.") == false)
        {
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Failed to send command packet.");
            return false;
        }

        sequenceNumber++;

        const bool ackOk = waitForCommandAck(clientSocket, logger);

        if (ackOk == false)
        {
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Command acknowledgement failed.");
            return false;
        }

        if (transitionState(state,
            agc::ServerState::ACTIVE,
            logger,
            "Command acknowledged successfully.") == false)
        {
            return false;
        }

        return true;
    }*/

    bool sendCommandAndAwaitAck(SOCKET clientSocket,
        agc::Logger& logger,
        agc::ServerState& state,
        std::uint32_t& sequenceNumber,
        const std::string& aircraftId,
        const std::string& commandText)
    {
        if (state != agc::ServerState::ACTIVE)
        {
            logger.logError("STATE", "Command sending attempted outside ACTIVE state.");
            return false;
        }

        state = agc::ServerState::COMMAND_PROCESSING;
        printState(state);

        const agc::Packet commandPacket =
            agc::makePacket(agc::MessageType::COMMAND,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(commandText));

        bool ackOk = false;

        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (sendAndLog(clientSocket, logger, commandPacket,
                "Command sent to client. Attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("COMMAND_SEND",
                    "Command send failed on attempt " + std::to_string(attempt));
                continue;
            }

            if (waitForCommandAck(clientSocket, logger) == true)
            {
                ackOk = true;
                break;
            }

            logger.logError("COMMAND_ACK",
                "Command ACK not received on attempt " + std::to_string(attempt));
        }

        if (ackOk == false)
        {
            state = agc::ServerState::ERROR_STATE;
            printState(state);
            return false;
        }

        sequenceNumber++;
        state = agc::ServerState::ACTIVE;
        printState(state);
        return true;
    }

    /*bool requestAdditionalStatus(SOCKET clientSocket,
        agc::Logger& logger,
        agc::ServerState& state,
        std::uint32_t& sequenceNumber,
        const std::string& aircraftId)
    {
        if (state != agc::ServerState::ACTIVE)
        {
            logger.logError("STATE", "Data request attempted outside ACTIVE state.");
            return false;
        }

        state = agc::ServerState::COMMAND_PROCESSING;
        printState(state);

        const agc::Packet requestPacket =
            agc::makePacket(agc::MessageType::DATA_REQUEST,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("REQUEST_ENGINE_STATUS"));

        if (sendAndLog(clientSocket, logger, requestPacket, "Additional status request sent.") == false)
        {
            return false;
        }

        sequenceNumber++;

        if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
        {
            logger.logError("TIMEOUT", "Timed out waiting for STATUS_RESPONSE.");
            return false;
        }

        agc::Packet responsePacket{};
        if (receiveAndLog(clientSocket, logger, responsePacket, "Status response received.") == false)
        {
            return false;
        }

        const bool ok =
            (static_cast<agc::MessageType>(responsePacket.header.messageType) == agc::MessageType::STATUS_RESPONSE);

        if (ok == true)
        {
            std::cout << "[SERVER] Additional status from aircraft: "
                << agc::payloadToString(responsePacket.payload) << "\n";
        }

        state = agc::ServerState::ACTIVE;
        printState(state);

        return ok;
    }*/

    bool requestAdditionalStatus(SOCKET clientSocket,
        agc::Logger& logger,
        agc::ServerState& state,
        std::uint32_t& sequenceNumber,
        const std::string& aircraftId)
    {
        if (state != agc::ServerState::ACTIVE)
        {
            logger.logError("STATE", "Data request attempted outside ACTIVE state.");
            return false;
        }

        state = agc::ServerState::COMMAND_PROCESSING;
        printState(state);

        const agc::Packet requestPacket =
            agc::makePacket(agc::MessageType::DATA_REQUEST,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("REQUEST_ENGINE_STATUS"));

        agc::Packet responsePacket{};
        bool ok = false;

        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            if (sendAndLog(clientSocket, logger, requestPacket,
                "Additional status request sent. Attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("DATA_REQUEST_SEND",
                    "Status request send failed on attempt " + std::to_string(attempt));
                continue;
            }

            if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
            {
                logger.logError("TIMEOUT",
                    "Timed out waiting for STATUS_RESPONSE on attempt " + std::to_string(attempt));
                continue;
            }

            if (receiveAndLog(clientSocket, logger, responsePacket,
                "Status response received. Attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("DATA_REQUEST_RX",
                    "Failed to receive STATUS_RESPONSE on attempt " + std::to_string(attempt));
                continue;
            }

            if (static_cast<agc::MessageType>(responsePacket.header.messageType) ==
                agc::MessageType::STATUS_RESPONSE)
            {
                ok = true;
                break;
            }

            logger.logError("DATA_REQUEST_TYPE",
                "Expected STATUS_RESPONSE on attempt " + std::to_string(attempt));
        }

        if (ok == true)
        {
            std::cout << "[SERVER] Additional status from aircraft: "
                << agc::payloadToString(responsePacket.payload) << "\n";

            sequenceNumber++;
            state = agc::ServerState::ACTIVE;
            printState(state);
            return true;
        }

        state = agc::ServerState::ERROR_STATE;
        printState(state);
        return false;
    }

    bool performLargeTransfer(SOCKET clientSocket,
        agc::Logger& logger,
        agc::ServerState& state,
        std::uint32_t& sequenceNumber,
        const std::string& aircraftId)
    {
        if (state != agc::ServerState::ACTIVE)
        {
            logger.logError("STATE", "Large transfer attempted outside ACTIVE state.");
            return false;
        }

        /*
          Stay in ACTIVE while sending the initiation command.
          That command internally transitions ACTIVE -> COMMAND_PROCESSING -> ACTIVE.
        */
        if (sendCommandAndAwaitAck(clientSocket,
            logger,
            state,
            sequenceNumber,
            aircraftId,
            "INITIATE_DIAGNOSTIC_TRANSFER") == false)
        {
            return false;
        }

        if (transitionState(state,
            agc::ServerState::DATA_TRANSFER,
            logger,
            "Beginning large diagnostic transfer.") == false)
        {
            return false;
        }

        const std::vector<std::uint8_t> largeBlob =
            agc::generateDiagnosticBlob(agc::LARGE_FILE_SIZE_BYTES);

        const std::uint32_t checksum = agc::computeChecksum(largeBlob);
        const std::uint32_t totalChunks =
            static_cast<std::uint32_t>(
                (largeBlob.size() + agc::LARGE_FILE_CHUNK_SIZE - 1U) /
                agc::LARGE_FILE_CHUNK_SIZE);

        const std::string startText =
            "filename=diagnostic_payload.bin;size=" + std::to_string(largeBlob.size()) +
            ";chunks=" + std::to_string(totalChunks) +
            ";checksum=" + std::to_string(checksum);

        const agc::Packet startPacket =
            agc::makePacket(agc::MessageType::LARGE_DATA_START,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(startText));

        if (sendAndLog(clientSocket, logger, startPacket, "Large transfer start sent.") == false)
        {
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Failed to send large transfer start packet.");
            return false;
        }

        sequenceNumber++;

        for (std::uint32_t chunkIndex = 0U; chunkIndex < totalChunks; ++chunkIndex)
        {
            const std::size_t startOffset =
                static_cast<std::size_t>(chunkIndex) * agc::LARGE_FILE_CHUNK_SIZE;

            std::size_t endOffset = startOffset + agc::LARGE_FILE_CHUNK_SIZE;
            if (endOffset > largeBlob.size())
            {
                endOffset = largeBlob.size();
            }

            const std::vector<std::uint8_t> chunkData(
                largeBlob.begin() + static_cast<std::ptrdiff_t>(startOffset),
                largeBlob.begin() + static_cast<std::ptrdiff_t>(endOffset));

            const agc::Packet chunkPacket =
                agc::makePacket(agc::MessageType::LARGE_DATA_CHUNK,
                    sequenceNumber,
                    aircraftId,
                    chunkData);

            if (sendAndLog(clientSocket, logger, chunkPacket, "Large data chunk sent.") == false)
            {
                (void)transitionState(state,
                    agc::ServerState::ERROR_STATE,
                    logger,
                    "Failed to send large data chunk.");
                return false;
            }

            sequenceNumber++;
        }

        const agc::Packet endPacket =
            agc::makePacket(agc::MessageType::LARGE_DATA_END,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload("TRANSFER_COMPLETE"));

        if (sendAndLog(clientSocket, logger, endPacket, "Large transfer end sent.") == false)
        {
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Failed to send large transfer end packet.");
            return false;
        }

        sequenceNumber++;

        /*if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
        {
            logger.logError("TIMEOUT", "Timed out waiting for transfer completion response.");
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Timeout waiting for transfer completion response.");
            return false;
        }

        agc::Packet finalStatus{};
        if (receiveAndLog(clientSocket, logger, finalStatus, "Transfer completion response received.") == false)
        {
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Failed to receive transfer completion response.");
            return false;
        }

        const bool ok =
            (static_cast<agc::MessageType>(finalStatus.header.messageType) == agc::MessageType::STATUS_RESPONSE);*/

        agc::Packet finalStatus{};
        const bool ok =
            receiveExpectedMessageWithRetransmission(clientSocket,
                logger,
                agc::MessageType::STATUS_RESPONSE,
                finalStatus,
                "Transfer completion response");

        if (ok == false)
        {
            logger.logError("TRANSFER_STATUS", "Expected STATUS_RESPONSE after large transfer.");
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Invalid response type after large transfer.");
            return false;
        }

        std::cout << "[SERVER] Transfer completion status: "
            << agc::payloadToString(finalStatus.payload) << "\n";

        if (transitionState(state,
            agc::ServerState::ACTIVE,
            logger,
            "Large transfer completed successfully.") == false)
        {
            return false;
        }

        return true;
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

    if (transitionState(state,
        agc::ServerState::IDLE_LISTENING,
        logger,
        "Server socket initialized and listening.") == false)
    {
        agc::closeSocket(listenSocket);
        agc::cleanupSockets();
        return 1;
    }
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

    (void)agc::setSocketTimeouts(clientSocket, agc::SOCKET_TIMEOUT_MS);

    if (transitionState(state,
        agc::ServerState::VERIFICATION,
        logger,
        "Client connection accepted; beginning verification.") == false)
    {
        agc::closeSocket(clientSocket);
        agc::closeSocket(listenSocket);
        agc::cleanupSockets();
        return 1;
    }

    agc::Packet firstPacket{};
    bool sessionActive = false;
    std::uint32_t responseSequence = 1U;

    if (receiveAndLog(clientSocket, logger, firstPacket, "First packet received.") == false)
    {
        std::cerr << "[SERVER] ERROR: failed to receive first packet.\n";
        logger.logError("VERIFY_RX", "Failed to receive first packet.");
        state = agc::ServerState::ERROR_STATE;
    }
    else
    {
        const bool typeValid =
            (static_cast<agc::MessageType>(firstPacket.header.messageType) == agc::MessageType::CONNECT_REQUEST);

        const bool packetValid = validateActivePacket(firstPacket);

        if ((typeValid == true) && (packetValid == true))
        {
            const std::string aircraftId = agc::safeAircraftIdToString(firstPacket.header.aircraftId);

            const agc::Packet ackPacket =
                agc::makePacket(agc::MessageType::CONNECT_ACK,
                    responseSequence,
                    aircraftId,
                    agc::stringToPayload("Connection verified."));

            if (sendAndLog(clientSocket, logger, ackPacket, "Connection ACK sent.") == true)
            {
                std::cout << "[SERVER] Client verified successfully.\n";
                sessionActive = true;
                if (transitionState(state,
                    agc::ServerState::ACTIVE,
                    logger,
                    "Client verification successful.") == false)
                {
                    sessionActive = false;
                    state = agc::ServerState::ERROR_STATE;
                }
                responseSequence++;
            }
            else
            {
                state = agc::ServerState::ERROR_STATE;
            }
        }
        else
        {
            const std::string aircraftId = agc::safeAircraftIdToString(firstPacket.header.aircraftId);
            const agc::Packet errorPacket =
                agc::makeErrorPacket(responseSequence, aircraftId, "Verification failed.");

            (void)sendAndLog(clientSocket, logger, errorPacket, "Verification failed.");
            std::cerr << "[SERVER] ERROR: client verification failed.\n";
            state = agc::ServerState::ERROR_STATE;
        }
    }

    std::uint32_t telemetryCount = 0U;
    bool transferTriggered = false;

    while (sessionActive == true)
    {
        agc::Packet incomingPacket{};

        if ((state != agc::ServerState::ACTIVE) &&
            (state != agc::ServerState::COMMAND_PROCESSING) &&
            (state != agc::ServerState::DATA_TRANSFER) &&
            (state != agc::ServerState::DISCONNECTING))
        {
            logger.logError("STATE_LOOP",
                "Invalid server state while session is active: " + stateToString(state));
            (void)transitionState(state,
                agc::ServerState::ERROR_STATE,
                logger,
                "Illegal state detected during active session loop.");
            break;
        }

        if (receiveAndLog(clientSocket, logger, incomingPacket, "Packet received during active session.") == false)
        {
            std::cerr << "[SERVER] ERROR: receive failed or timeout occurred.\n";
            logger.logError("ACTIVE_RX", "Receive failed during active session.");
            state = agc::ServerState::ERROR_STATE;
            break;
        }

        const agc::MessageType messageType =
            static_cast<agc::MessageType>(incomingPacket.header.messageType);

        if (messageType == agc::MessageType::TELEMETRY)
        {
            const bool packetValid = validateActivePacket(incomingPacket);

            agc::TelemetryData telemetry{};
            const bool telemetryValid = agc::payloadToTelemetry(incomingPacket.payload, telemetry);

            if ((packetValid == true) && (telemetryValid == true))
            {
                telemetryCount++;

                std::cout << "\n[SERVER] Telemetry received\n";
                std::cout << "  Altitude (ft): " << telemetry.altitudeFt << "\n";
                std::cout << "  Speed (knots): " << telemetry.speedKnots << "\n";
                std::cout << "  Heading (deg): " << telemetry.headingDeg << "\n";
                std::cout << "  Fuel (%): " << telemetry.fuelPercent << "\n";

                const std::string aircraftId =
                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

                const agc::Packet ackPacket =
                    agc::makePacket(agc::MessageType::TELEMETRY_ACK,
                        responseSequence,
                        aircraftId,
                        agc::stringToPayload("Telemetry accepted."));

                if (sendAndLog(clientSocket, logger, ackPacket, "Telemetry ACK sent.") == false)
                {
                    state = agc::ServerState::ERROR_STATE;
                    break;
                }

                responseSequence++;

                if (telemetryCount == 2U)
                {
                    if (sendCommandAndAwaitAck(clientSocket,
                        logger,
                        state,
                        responseSequence,
                        aircraftId,
                        "ADJUST_HEADING:100") == false)
                    {
                        state = agc::ServerState::ERROR_STATE;
                        break;
                    }
                }

                if (telemetryCount == 3U)
                {
                    if (requestAdditionalStatus(clientSocket,
                        logger,
                        state,
                        responseSequence,
                        aircraftId) == false)
                    {
                        state = agc::ServerState::ERROR_STATE;
                        break;
                    }
                }

                if ((telemetryCount == 5U) && (transferTriggered == false))
                {
                    if (performLargeTransfer(clientSocket,
                        logger,
                        state,
                        responseSequence,
                        aircraftId) == false)
                    {
                        state = agc::ServerState::ERROR_STATE;
                        break;
                    }

                    transferTriggered = true;
                }
            }
            else
            {
                const std::string aircraftId =
                    agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

                const agc::Packet errorPacket =
                    agc::makeErrorPacket(responseSequence, aircraftId, "Invalid telemetry packet.");

                (void)sendAndLog(clientSocket, logger, errorPacket, "Invalid telemetry packet.");
                state = agc::ServerState::ERROR_STATE;
                break;
            }
        }
        else if (messageType == agc::MessageType::DISCONNECT)
        {
            if (transitionState(state,
                agc::ServerState::DISCONNECTING,
                logger,
                "Disconnect request received from client.") == false)
            {
                state = agc::ServerState::ERROR_STATE;
                break;
            }

            std::cout << "[SERVER] Disconnect request received.\n";
            logger.logText("Disconnect request received.");

            const std::string aircraftId =
                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

            const agc::Packet disconnectAck =
                agc::makePacket(agc::MessageType::DISCONNECT,
                    responseSequence,
                    aircraftId,
                    agc::stringToPayload("Session closed."));

            (void)sendAndLog(clientSocket, logger, disconnectAck, "Disconnect ACK sent.");
            sessionActive = false;
        }
        else
        {
            const std::string aircraftId =
                agc::safeAircraftIdToString(incomingPacket.header.aircraftId);

            const agc::Packet errorPacket =
                agc::makeErrorPacket(responseSequence, aircraftId, "Unexpected message type.");

            (void)sendAndLog(clientSocket, logger, errorPacket, "Unexpected message type.");
            logger.logError("UNEXPECTED_TYPE", "Unexpected message type in active session.");
            state = agc::ServerState::ERROR_STATE;
            break;
        }
    }

    if (state == agc::ServerState::ERROR_STATE)
    {
        logger.logError("SERVER_STATE", "Server entered ERROR_STATE.");
        printState(state);
    }

    agc::closeSocket(clientSocket);
    agc::closeSocket(listenSocket);

    if (transitionState(state,
        agc::ServerState::SHUTDOWN,
        logger,
        "Server shutdown sequence complete.") == false)
    {
        logger.logError("SHUTDOWN", "Invalid transition to SHUTDOWN.");
    }
    logger.logText("Server shutdown complete.");

    agc::cleanupSockets();
    return exitCode;
}