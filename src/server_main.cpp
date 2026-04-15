/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian
File     : server_main.cpp

Purpose:
This file implements the ground control server application. The server:
- starts a listening TCP socket,
- accepts a client connection,
- verifies the first packet before allowing active communication,
- receives and validates telemetry,
- sends acknowledgements,
- sends operational commands,
- requests additional aircraft status,
- initiates and completes the required large-data transfer,
- enforces state-machine behavior,
- and performs controlled shutdown.

Importance of this file:
The server is the controlling side of the distributed system. It is responsible
for deciding when major actions occur and for ensuring communication happens only
in valid states.

Main responsibilities in this file:
- state-machine control,
- connection acceptance and verification,
- packet validation during active session,
- command dispatch and acknowledgement handling,
- additional status request handling,
- large transfer initiation and completion,
- error handling and shutdown.

DAL-oriented commentary:
- DAL A style relevance:
  Verification-stage checks, state-transition protection, payload/timestamp/ID
  validation, and defensive retransmission handling act as the strongest safety
  barriers in this file.
- DAL B style relevance:
  Telemetry handling, command timing, and large-transfer initiation affect major
  distributed operational behavior.
- DAL C style relevance:
  Logging, status request handling, transfer sequencing, and orderly disconnect
  support reliability, traceability, and diagnostics.
- DAL D/E style relevance:
  Console display and some helper formatting are supportive rather than primary
  control logic.
*/

#include "common.hpp"
#include "server_logic.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    /*
    Forward declarations are placed here because several helpers call each other.
    This keeps the implementation organized while still allowing a top-down read
    of the file.
    */
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

    /*
    printState

    Purpose:
    Prints the current server state to the console for operator visibility.

    Importance of this helper:
    The project includes a server-side state machine, and visible state output
    helps both during debugging and during demonstration.

    DAL reasoning:
    This is mainly DAL D/C-style supportive behavior. It does not control the
    state machine, but it improves observability of state transitions.
    */
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

    /*
    isValidTransition

    Purpose:
    Defines which state transitions are allowed in the server state machine.

    Importance of this helper:
    A state machine is only useful if it rejects illegal transitions. This
    function centralizes the allowed transition rules.

    DAL reasoning:
    This is one of the most important defensive controls in the file and is
    conceptually closest to DAL A/B because it prevents uncontrolled or unsafe
    operational mode changes.

    Logic explanation:
    For each current state, only the explicitly listed next states are allowed.
    Any other requested transition is rejected.
    */
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

    /*
    stateToString

    Purpose:
    Converts ServerState to a readable string for logs and error reporting.

    Importance of this helper:
    Logging transition attempts and failures is much clearer when state names are
    written as text instead of numeric values.

    DAL reasoning:
    This is supportive DAL D/C-style behavior for traceability.
    */
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

    /*
    transitionState

    Purpose:
    Performs a checked server-state transition and logs the result.

    Importance of this helper:
    All state changes should pass through one controlled location. This reduces
    the chance of silent or illegal state mutation elsewhere in the file.

    DAL reasoning:
    This is one of the most important DAL A/B-style controls in the server
    implementation because it centrally enforces legal state-machine behavior.

    Logic explanation:
    1. Check whether the requested transition is legal.
    2. If illegal, log an error and reject it.
    3. If legal, log the transition, update the state, and print it.
    */
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

    /*
    sendAndLog

    Purpose:
    Sends a packet to the client and records the result in the server log.

    Importance of this helper:
    Sending plus logging happens throughout the server. Centralizing the pattern
    improves consistency and reduces code duplication.

    DAL reasoning:
    This helper supports reliable communication and traceability, making it
    conceptually DAL B/C.
    */
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

    /*
    receiveAndLog

    Purpose:
    Receives a packet from the client and records the result in the server log.

    Importance of this helper:
    Like sendAndLog(), it centralizes a repeated communication pattern and keeps
    receive-side traceability consistent.

    DAL reasoning:
    This helper supports operational traceability and is conceptually DAL B/C.
    */
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

    /*
    validateActivePacket

    Purpose:
    Delegates active-session validation to the server_logic module.

   Importance of this helper:
    Keeping validation logic in a separate module makes it easier to unit test
    and keeps server_main.cpp focused on communication flow.

    DAL reasoning:
    Active-packet validation is conceptually DAL A/B because it acts as a
    defensive gate before processing active-session data.
    */
    bool validateActivePacket(const agc::Packet& packet)
    {
        return agc::server_logic::validateActivePacket(packet);
    }

    /*
    waitForCommandAck

    Purpose:
    Waits for a COMMAND_ACK packet after the server has issued a command.

    Importance of this helper:
    Command dispatch should be confirmed by the client before the server returns
    to ACTIVE operation.

    DAL reasoning:
    This is conceptually DAL B because command acknowledgement affects major
    control-flow correctness.
    */
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

    /*
    sendPacketWithRetransmission

    Purpose:
    Sends a packet with bounded retransmission attempts.

    Importance of this helper:
    Some server-originated packets are important enough to retry if send fails.

    Logic explanation:
    - attempt send up to MAX_RETRANSMISSION_ATTEMPTS times
    - log success or per-attempt failure
    - stop immediately once one attempt succeeds

    DAL reasoning:
    This is conceptually DAL A/B-style defensive communication behavior.
    */
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

    /*
    receiveExpectedMessageWithRetransmission

    Purpose:
    Waits for a specific expected message type with bounded retry behavior.

    Importance of this helper:
    The server should not immediately fail a critical exchange because of one
    timeout, one wrong packet, or one temporary receive issue.

    Logic explanation:
    For each attempt:
    1. Wait for the socket to become readable.
    2. Try to receive and log a packet.
    3. Check whether the packet type matches expectation.
    4. Retry on timeout, receive failure, or wrong packet type.

    DAL reasoning:
    This is conceptually DAL A/B-style defensive protocol handling.
    */
    bool receiveExpectedMessageWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        agc::MessageType expectedType,
        agc::Packet& receivedPacket,
        const std::string& retryContext)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            // Wait with timeout so the server does not block forever.
            if (agc::waitForReadable(clientSocket, agc::SOCKET_TIMEOUT_MS) == false)
            {
                logger.logError("RETRANSMIT_TIMEOUT",
                    retryContext + " timed out on attempt " + std::to_string(attempt));
                continue;
            }

            // Attempt to receive the expected packet.
            if (receiveAndLog(clientSocket, logger, receivedPacket,
                retryContext + " received on attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("RETRANSMIT_RX",
                    retryContext + " receive failed on attempt " + std::to_string(attempt));
                continue;
            }

            // Succeed only if the message type matches the expected one.
            if (static_cast<agc::MessageType>(receivedPacket.header.messageType) == expectedType)
            {
                return true;
            }

            logger.logError("RETRANSMIT_TYPE",
                retryContext + " wrong message type on attempt " + std::to_string(attempt));
        }

        return false;
    }

    /*
    sendCommandAndAwaitAck

    Purpose:
    Sends a command to the client and waits for command acknowledgement.

    Importance of this helper:
    The server issues a command as part of the active communication sequence and
    must confirm that the client acknowledges it before continuing.

    Logic explanation:
    1. Ensure the server is currently in ACTIVE.
    2. Move into COMMAND_PROCESSING.
    3. Build the COMMAND packet.
    4. Try sending it with bounded retry behavior.
    5. Wait for COMMAND_ACK after each send attempt.
    6. On success, increment sequence and return to ACTIVE.
    7. On failure, move to ERROR_STATE.

    DAL reasoning:
    This is conceptually DAL B because it governs an important control exchange.
    */
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

        // Move into command-processing mode while waiting for the client to acknowledge.
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

        // Only advance sequence after successful command exchange.
        sequenceNumber++;
        state = agc::ServerState::ACTIVE;
        printState(state);
        return true;
    }

    /*
    requestAdditionalStatus

    Purpose:
    Sends a DATA_REQUEST packet and waits for a STATUS_RESPONSE.

    Importance of this helper:
    The project includes a server-triggered status query after a certain point in
    the telemetry sequence.

    Logic explanation:
    1. Ensure the server is currently in ACTIVE.
    2. Move into COMMAND_PROCESSING.
    3. Send a DATA_REQUEST packet.
    4. Wait for STATUS_RESPONSE with bounded retry behavior.
    5. On success, print the returned status and return to ACTIVE.
    6. On failure, move to ERROR_STATE.

    DAL reasoning:
    This is conceptually DAL B/C because it controls structured information flow
    beyond the basic telemetry cycle.
    */
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

        // Enter command-processing mode while waiting for a structured response.
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

    /*
    performLargeTransfer

    Purpose:
    Executes the entire server-side large-data transfer sequence.

    Importance of this helper:
    The project requires the server to initiate a large (>1 MB) object transfer
    to the client. This helper performs that feature in a controlled sequence.

    Logic explanation:
    1. Ensure the server is in ACTIVE.
    2. Send a command instructing the client to prepare for diagnostic transfer.
    3. Transition into DATA_TRANSFER state.
    4. Generate the large payload and compute checksum.
    5. Compute chunk count and build transfer metadata.
    6. Send LARGE_DATA_START metadata packet.
    7. Send each LARGE_DATA_CHUNK packet.
    8. Send LARGE_DATA_END marker.
    9. Wait for final STATUS_RESPONSE from the client.
    10. Return to ACTIVE only if the transfer completes successfully.

    DAL reasoning:
    This is one of the most important DAL B/A-style operational blocks in the
    server because it controls a required major system feature and includes
    integrity-related verification flow.
    */
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

        // Create deterministic binary content for the required >1 MB transfer.
        const std::vector<std::uint8_t> largeBlob =
            agc::generateDiagnosticBlob(agc::LARGE_FILE_SIZE_BYTES);

        // Compute checksum once so the client can verify reconstruction integrity.
        const std::uint32_t checksum = agc::computeChecksum(largeBlob);

        const std::uint32_t totalChunks =
            agc::server_logic::computeTotalChunks(largeBlob.size(),
                agc::LARGE_FILE_CHUNK_SIZE);

        // Build the structured metadata string that the client will parse first.
        const std::string startText =
            agc::server_logic::buildTransferStartText("diagnostic_payload.bin",
                largeBlob.size(),
                totalChunks,
                checksum);

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

        // Send each chunk in order using the configured chunk size.
        for (std::uint32_t chunkIndex = 0U; chunkIndex < totalChunks; ++chunkIndex)
        {
            const std::size_t startOffset =
                static_cast<std::size_t>(chunkIndex) * agc::LARGE_FILE_CHUNK_SIZE;

            std::size_t endOffset = startOffset + agc::LARGE_FILE_CHUNK_SIZE;
            if (endOffset > largeBlob.size())
            {
                // The final chunk may be smaller than the configured chunk size.
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

        // Signal that all chunks have now been sent.
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

        // Wait for the client to report whether reconstruction and checksum validation succeeded.
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
    /*
    SERVER APPLICATION ENTRY POINT
    */

    int exitCode = 0;
    agc::ServerState state = agc::ServerState::STARTUP;
    printState(state);

    // File-based logging is required and supports full session traceability.
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

    // Apply bounded socket timeouts to keep protocol steps controlled.
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

    /*
    VERIFICATION STAGE

    The very first packet must be a valid CONNECT_REQUEST from the expected
    aircraft endpoint before the server allows ACTIVE communication.
    */
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

    /*
    ACTIVE SESSION LOOP

    Once verification succeeds, the server stays here until disconnect or error.
    */
    while (sessionActive == true)
    {
        agc::Packet incomingPacket{};

        // Defensive state guard: sessionActive should never coexist with an illegal state.
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

                // Display the received telemetry so the session is observable during execution/demo.
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

                // Trigger command flow at the configured telemetry count.
                if (agc::server_logic::shouldSendCommand(telemetryCount) == true)
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

                // Trigger additional status request at the configured telemetry count.
                if (agc::server_logic::shouldRequestAdditionalStatus(telemetryCount) == true)
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

                // Trigger large transfer once at the configured telemetry count.
                if (agc::server_logic::shouldStartLargeTransfer(telemetryCount,
                    transferTriggered) == true)
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
            // Any unexpected message type is treated as a protocol violation.
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

    /*
    SHUTDOWN / ERROR HANDLING
    */
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