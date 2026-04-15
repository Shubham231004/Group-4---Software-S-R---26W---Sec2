/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian
File     : client_main.cpp

Purpose:
This file implements the aircraft-side client application. The client:
- establishes a TCP connection to the server,
- performs connection verification,
- periodically sends telemetry,
- handles optional command/data-transfer messages from the server,
- receives and reconstructs a large diagnostic file,
- returns transfer completion status,
- and disconnects cleanly.

Importance of this file:
The client represents the in-flight aircraft endpoint in the distributed system.
It is responsible for producing telemetry, responding to server requests, and
participating in the required large-object transfer feature.

Operational responsibilities in this file:
- connection setup and retry handling,
- handshake retransmission handling,
- telemetry transmission with acknowledgement/retransmission,
- command acknowledgement,
- additional status response,
- large transfer reception and validation,
- orderly disconnect sequence.

DAL-oriented commentary:
- DAL A style relevance:
  Connection verification, metadata validation, retransmission handling, and
  large-transfer integrity checks act as defensive barriers against invalid or
  uncontrolled communication.
- DAL B style relevance:
  Telemetry sending, server command handling, and transfer-response behavior
  affect major operational behavior between client and server.
- DAL C style relevance:
  Logging, status reporting, optional-message handling, and file reconstruction
  support reliability, traceability, and diagnostics.
- DAL D/E style relevance:
  Console messaging and some convenience helpers are lower direct criticality,
  but still important for usability and maintainability.
*/

#include "common.hpp"
#include "client_logic.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    /*
    generateTelemetry

    Purpose:
    Delegates telemetry generation to the client_logic module.

    Importance of this helper:
    client_main.cpp is responsible for communication flow, while the logic module
    is responsible for generating the data values themselves. Keeping generation
    logic separate improves modularity and testability.

    DAL reasoning:
    Telemetry values directly affect what the server receives and processes, so
    this path is conceptually relevant to DAL B/C.
    */
    agc::TelemetryData generateTelemetry(std::uint32_t sampleIndex)
    {
        return agc::client_logic::generateTelemetry(sampleIndex);
    }

    /*
    connectWithRetries

    Purpose:
    Attempts to connect to the server multiple times before failing.

    Importance of this helper:
    Transient startup timing issues are common in client/server systems. The
    server may not yet be ready at the exact moment the client starts.

    Logic explanation:
    - Try to connect up to MAX_RECONNECT_ATTEMPTS times.
    - Wait 1 second between attempts.
    - Return true immediately if any attempt succeeds.
    - Return false only after all attempts fail.

    DAL reasoning:
    This has DAL A/B-style defensive relevance because controlled retry behavior
    helps avoid immediate unsafe failure due to timing mismatch.
    */
    bool connectWithRetries(SOCKET clientSocket, const sockaddr_in& serverAddress)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RECONNECT_ATTEMPTS; ++attempt)
        {
            std::cout << "[CLIENT] Connection attempt " << attempt << "...\n";

            // Try a normal TCP connect for this attempt.
            if (connect(clientSocket,
                reinterpret_cast<const sockaddr*>(&serverAddress),
                static_cast<int>(sizeof(serverAddress))) != SOCKET_ERROR)
            {
                return true;
            }

            // Wait briefly before the next retry so the server has time to become ready.
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return false;
    }

    /*
    sendAndLog

    Purpose:
    Sends a packet and records the result in the client log.

    Importance of this helper:
    The same send + log pattern appears many times in the client. Centralizing it
    reduces duplication and makes behavior consistent.

    Logic explanation:
    - Call agc::sendPacket()
    - If successful, log a TX packet entry
    - If not successful, log an error entry
    - Return the send result to the caller

    DAL reasoning:
    Logging and transmission together support traceability and reliable protocol
    execution, making this helper conceptually relevant to DAL B/C.
    */
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

    /*
    receiveAndLog

    Purpose:
    Receives a packet and records the result in the client log.

    Importance of this helper:
    Like sendAndLog(), this centralizes a repeated communication pattern so all
    client receive operations are logged consistently.

    DAL reasoning:
    Packet reception and logging are operationally important for both debugging
    and communication traceability, so this helper is conceptually DAL B/C.
    */
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

    /*
    receiveExpectedPacketWithRetransmission

    Purpose:
    Waits for a specific expected packet type with bounded retransmission-style
    retry behavior.

    Importance of this helper:
    Important protocol exchanges such as CONNECT_ACK and TELEMETRY_ACK should not
    fail immediately because of one missed read or timing issue.

    Logic explanation:
    For each attempt:
    1. Wait for the socket to become readable.
    2. Try to receive and log a packet.
    3. Check whether the received message type matches the expected type.
    4. If any of these steps fail, log the error and continue retrying.
    5. If all attempts fail, return false.

    DAL reasoning:
    This is a strong DAL A/B-style defensive mechanism because it prevents single
    transient failures from immediately breaking safety-relevant communication
    steps.
    */
    bool receiveExpectedPacketWithRetransmission(SOCKET socketHandle,
        agc::Logger& logger,
        agc::MessageType expectedType,
        agc::Packet& packet,
        const std::string& context)
    {
        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            // Wait for incoming data instead of blocking forever.
            if (agc::waitForReadable(socketHandle, agc::SOCKET_TIMEOUT_MS) == false)
            {
                logger.logError("RETRANSMIT_TIMEOUT",
                    context + " timed out on attempt " + std::to_string(attempt));
                continue;
            }

            // Try to receive the incoming packet for this attempt.
            if (receiveAndLog(socketHandle, logger, packet,
                context + " received on attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("RETRANSMIT_RX",
                    context + " receive failed on attempt " + std::to_string(attempt));
                continue;
            }

            // Only succeed if the received packet type is exactly what was expected.
            if (static_cast<agc::MessageType>(packet.header.messageType) == expectedType)
            {
                return true;
            }

            logger.logError("RETRANSMIT_TYPE",
                context + " wrong packet type on attempt " + std::to_string(attempt));
        }

        return false;
    }

    /*
    sendTelemetryWithRetransmission

    Purpose:
    Sends a telemetry packet and waits for TELEMETRY_ACK with retry handling.

    Importance of this helper:
    Telemetry is the main repeating communication path in the client. It should
    not fail permanently because of one lost acknowledgement.

    Logic explanation:
    For each attempt:
    - send the telemetry packet,
    - wait for a TELEMETRY_ACK,
    - if acknowledged, increment sequence number and return success,
    - otherwise log the failure and retry.

    DAL reasoning:
    Telemetry acknowledgement is a major part of the client/server protocol, so
    this helper is conceptually DAL A/B-style in terms of reliable communication.
    */
    bool sendTelemetryWithRetransmission(SOCKET clientSocket,
        agc::Logger& logger,
        const agc::Packet& telemetryPacket,
        std::uint32_t& sequenceNumber)
    {
        agc::Packet ackPacket{};

        for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
        {
            // Send the telemetry packet for this attempt.
            if (sendAndLog(clientSocket, logger, telemetryPacket,
                "Telemetry sent. Attempt " + std::to_string(attempt)) == false)
            {
                logger.logError("TELEMETRY_SEND",
                    "Telemetry send failed on attempt " + std::to_string(attempt));
                continue;
            }

            // Wait for the expected acknowledgement packet.
            if (receiveExpectedPacketWithRetransmission(clientSocket,
                logger,
                agc::MessageType::TELEMETRY_ACK,
                ackPacket,
                "Telemetry ACK") == true)
            {
                // Advance the sequence only after successful acknowledgement.
                sequenceNumber++;
                std::cout << "[CLIENT] Telemetry ACK received.\n";
                return true;
            }

            logger.logError("TELEMETRY_ACK",
                "Telemetry ACK not received on attempt " + std::to_string(attempt));
        }

        return false;
    }

    /*
    handleCommand

    Purpose:
    Processes a COMMAND packet received from the server and sends a COMMAND_ACK.

    Importance of this helper:
    The server may issue control instructions to the client during the active
    session. The client must acknowledge those instructions in a structured way.

    Logic explanation:
    - Read the command text from packet payload
    - Print it for operator visibility
    - Create a COMMAND_ACK packet indicating execution
    - Send it back to the server
    - Advance the sequence number

    DAL reasoning:
    Command handling affects major operational behavior, so this helper is
    conceptually DAL B.
    */
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

    /*
    handleDataRequest

    Purpose:
    Processes a DATA_REQUEST packet and returns a STATUS_RESPONSE packet.

    Importance of this helper:
    The server may request additional status information beyond the standard
    telemetry stream. This helper generates that structured response.

    Logic explanation:
    - Extract request text from payload
    - Print request information
    - Build a fixed diagnostic status string
    - Place it in a STATUS_RESPONSE packet
    - Send response and advance sequence number

    DAL reasoning:
    Additional status exchange supports operational monitoring and is closest to
    DAL B/C in relevance.
    */
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

    /*
    receiveLargeTransfer

    Purpose:
    Handles the entire client-side reception of a chunked large-data transfer.

    Importance of this helper:
    One of the key project requirements is transferring a large object (>1 MB)
    from server to client. This function performs the client-side receive,
    reconstruction, verification, and reporting steps.

    Logic explanation:
    1. Read and parse transfer metadata from the start packet.
    2. Reserve buffer space for the full transfer.
    3. Receive each expected chunk and append it to the buffer.
    4. Receive the transfer-end marker packet.
    5. Compute checksum and compare to expected checksum.
    6. Write reconstructed bytes to a file.
    7. Build and send STATUS_RESPONSE summarizing success/failure.
    8. Return true only if send succeeded and integrity checks passed.

    DAL reasoning:
    This is one of the highest-significance client-side helpers in the file,
    conceptually closest to DAL A/B because it governs safe handling of a major
    required transfer feature and performs integrity verification.
    */
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
            // Parse metadata from the LARGE_DATA_START payload.
            // This is a defensive gate: if metadata is malformed, transfer must stop.
            if (agc::client_logic::parseTransferMetadata(startText,
                expectedSize,
                expectedChunks,
                expectedChecksum) == false)
            {
                logger.logError("TRANSFER_META", "Invalid transfer metadata.");
                return false;
            }
        }

        // Reserve expected size up front to reduce reallocations during chunk assembly.
        std::vector<std::uint8_t> fileBuffer{};
        fileBuffer.reserve(expectedSize);

        // Receive the expected number of chunks and append each chunk payload.
        for (std::uint32_t chunkCount = 0U; chunkCount < expectedChunks; ++chunkCount)
        {
            agc::Packet chunkPacket{};

            if (receiveAndLog(clientSocket, logger, chunkPacket, "Large transfer chunk received.") == false)
            {
                return false;
            }

            // Only chunk packets are valid during this phase.
            if (static_cast<agc::MessageType>(chunkPacket.header.messageType) != agc::MessageType::LARGE_DATA_CHUNK)
            {
                logger.logError("TRANSFER_TYPE", "Expected LARGE_DATA_CHUNK.");
                return false;
            }

            // Append the chunk data to the reconstruction buffer.
            fileBuffer.insert(fileBuffer.end(), chunkPacket.payload.begin(), chunkPacket.payload.end());
        }

        // After all chunks, the client expects a transfer-end marker packet.
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

        // Compute actual integrity results after reconstruction.
        const std::uint32_t actualChecksum = agc::computeChecksum(fileBuffer);
        const bool sizeOk = (fileBuffer.size() == expectedSize);
        const bool checksumOk = (actualChecksum == expectedChecksum);

        // Persist the received file locally as proof of transfer completion.
        std::ofstream outputFile("received_diagnostic_payload.bin", std::ios::binary);
        if (outputFile.is_open())
        {
            outputFile.write(reinterpret_cast<const char*>(fileBuffer.data()),
                static_cast<std::streamsize>(fileBuffer.size()));
            outputFile.close();
        }

        // Build structured transfer-completion status for the server.
        const std::string statusText =
            agc::client_logic::buildTransferStatusText((sizeOk && checksumOk),
                fileBuffer.size(),
                actualChecksum);

        const agc::Packet statusPacket =
            agc::makePacket(agc::MessageType::STATUS_RESPONSE,
                sequenceNumber,
                aircraftId,
                agc::stringToPayload(statusText));

        const bool ok = sendAndLog(clientSocket, logger, statusPacket, "Transfer completion response sent.");
        sequenceNumber++;

        std::cout << "[CLIENT] Large transfer complete. File saved as received_diagnostic_payload.bin\n";

        // The transfer is considered fully successful only if:
        // 1. the status packet was sent successfully,
        // 2. the received size matched expectation,
        // 3. the checksum matched expectation.
        return ok && sizeOk && checksumOk;
    }

    /*
    handleOptionalServerMessage

    Purpose:
    Checks whether the server has sent an optional message after the main ACK
    flow and dispatches it to the appropriate handler.

    Importance of this helper:
    After telemetry acknowledgement, the server may optionally send:
    - COMMAND
    - DATA_REQUEST
    - LARGE_DATA_START

    Instead of forcing these into the main telemetry send logic, this helper
    centralizes optional-message processing.

    Logic explanation:
    - Wait briefly for optional data
    - If nothing arrives, return true (not an error)
    - If a packet arrives, dispatch by message type
    - If it is a large transfer, update the largeTransferCompleted flag

    DAL reasoning:
    This helper is conceptually DAL B/C because it supports controlled handling
    of optional but important server-driven behavior.
    */
    bool handleOptionalServerMessage(SOCKET clientSocket,
        agc::Logger& logger,
        const std::string& aircraftId,
        std::uint32_t& sequenceNumber,
        bool& largeTransferCompleted)
    {
        // Optional messages are not always present, so a short wait timeout is used.
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

        // Unknown optional messages are currently ignored rather than treated as fatal here.
        return true;
    }
}

int main()
{
    /*
    CLIENT APPLICATION ENTRY POINT
    */

    if (agc::initializeSockets() == false)
    {
        std::cerr << "[CLIENT] ERROR: WSAStartup failed.\n";
        return 1;
    }

    // File-based logging is a project requirement and important for traceability.
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

    // The project uses loopback communication for local testing.
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr) != 1)
    {
        std::cerr << "[CLIENT] ERROR: invalid server address.\n";
        agc::closeSocket(clientSocket);
        agc::cleanupSockets();
        return 1;
    }

    // Apply send/receive timeout protection to avoid indefinite blocking.
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

    /*
    HANDSHAKE / CONNECTION VERIFICATION
    */
    agc::Packet responsePacket{};
    bool connectOk = false;

    for (std::uint32_t attempt = 1U; attempt <= agc::MAX_RETRANSMISSION_ATTEMPTS; ++attempt)
    {
        // Build a connection request packet for this attempt.
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

        // Wait for CONNECT_ACK from the server.
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

    /*TELEMETRY LOOP*/
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

        // Send telemetry with acknowledgement-based retransmission.
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

        // After each telemetry ACK, the server may optionally send another message.
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

        // Delay between telemetry samples to simulate periodic aircraft updates.
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    /*    
    POST-TELEMETRY OPTIONAL WINDOW

    Purpose:
    After the main telemetry loop, the server may still begin the large transfer.
    This loop gives the client a short bounded period to handle that event.
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

    /*    CLEAN DISCONNECT    */
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

        // The disconnect acknowledgement is helpful, but the client treats it as optional
        // if the socket closes quickly after a valid end-of-session exchange.
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