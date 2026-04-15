#include "server_logic.hpp"

/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian
File     : server_logic.cpp

Purpose:
This file implements helper logic used specifically by the ground control server.
These functions support the server's higher-level operational decisions without
directly performing low-level socket communication.

Main responsibilities implemented here:
- validating whether an incoming packet is safe to process in ACTIVE state,
- calculating the number of chunks needed for large-data transfer,
- building the metadata string for the start of large transfer,
- deciding when to send a command to the client,
- deciding when to request additional aircraft status,
- deciding when to initiate a large data transfer.

Importance of this file:
The server is the controlling side of the distributed system. Even though the
functions here are small, they influence the correctness of:
- active-session packet acceptance,
- command timing,
- status-request timing,
- and large-transfer execution.

If this logic is wrong, the main server application may still compile and run,
but it may behave incorrectly or at the wrong phase of operation.

DAL-oriented commentary:
- DAL A style relevance:
  Active-packet validation is the most safety-relevant item in this file because
  it determines whether the server should trust incoming packets in the active
  communication phase.
- DAL B style relevance:
  Command timing and large-transfer initiation affect major distributed behavior.
- DAL C style relevance:
  Chunk counting and metadata-building support correctness and reliability of the
  large-file transfer process.
- DAL D/E style relevance:
  This file mainly contains operational decision logic rather than cosmetic or
  purely supportive behavior.
*/

namespace agc::server_logic
{
    /*
    validateActivePacket

    Purpose:
    Performs combined validation on a packet that has been received while the
    server is already inside the ACTIVE operational phase.

    Importance of this function:
    Once the server has entered ACTIVE, it expects ongoing communication from
    the verified aircraft client. However, that does not mean every packet
    should automatically be trusted. The server still needs to validate:
    - aircraft identity,
    - packet timestamp freshness,
    - payload size bounds.

    DAL reasoning:
    This function is conceptually closest to DAL A/B because it acts as a
    defensive acceptance gate before active-session data is processed.

    Logic explanation:
    The function computes three separate validation results and only returns
    true if all three are true.
    */
    bool validateActivePacket(const Packet& packet)
    {
        // Check that the packet still claims to come from the expected aircraft.
        const bool idValid = agc::validateAircraftId(packet.header.aircraftId);

        // Check that the packet timestamp is still within the allowed drift.
        // This helps reject stale or abnormally delayed packets.
        const bool timeValid =
            agc::validateTimestamp(packet.header.timestampMs, agc::getCurrentTimeMs());

        // Check that the payload size remains within the allowed protocol bound.
        const bool sizeValid = agc::validatePayloadSize(packet.header.payloadSize);

        // The packet is acceptable only if all validation checks succeed.
        return (idValid && timeValid && sizeValid);
    }

    /*
    computeTotalChunks

    Purpose:
    Computes how many chunks are required to transfer a large payload when the
    server must split it into bounded pieces.

    Importance of this function:
    The project requires transferring a payload larger than 1 MB, but packets
    still need to remain bounded in size. Therefore, the server must divide the
    data into multiple smaller chunks.

    DAL reasoning:
    This is conceptually closest to DAL C because it directly affects the
    reliability and correctness of the large-transfer feature.

    Logic explanation:
    - If chunkSize is zero, return zero immediately to avoid division by zero.
    - Otherwise, use a round-up division formula:
        (totalSize + chunkSize - 1) / chunkSize
      so that any partial remainder still counts as one final chunk.
    */
    std::uint32_t computeTotalChunks(std::size_t totalSize,
        std::size_t chunkSize)
    {
        // Defensive check: zero chunk size would make chunking undefined.
        if (chunkSize == 0U)
        {
            return 0U;
        }

        // Round up so even a partially filled final chunk is counted.
        return static_cast<std::uint32_t>(
            (totalSize + chunkSize - 1U) / chunkSize);
    }

    /*
    buildTransferStartText

    Purpose:
    Builds the structured metadata string that is sent in the LARGE_DATA_START
    packet before chunk transmission begins.

    Importance of this function:
    The client needs to know what kind of transfer is about to happen before it
    starts receiving chunks. This metadata string tells the client:
    - the file name,
    - the full byte size,
    - the number of chunks expected,
    - and the checksum it should verify against later.

    DAL reasoning:
    This is closest to DAL B/C because it supports a major system feature and
    must remain internally consistent for transfer reconstruction to succeed.

    Logic explanation:
    The function concatenates all metadata fields into one predictable string
    with semicolon-separated key/value entries.
    */
    std::string buildTransferStartText(const std::string& fileName,
        std::size_t totalSize,
        std::uint32_t totalChunks,
        std::uint32_t checksum)
    {
        return "filename=" + fileName +
            ";size=" + std::to_string(totalSize) +
            ";chunks=" + std::to_string(totalChunks) +
            ";checksum=" + std::to_string(checksum);
    }

    /*
    shouldSendCommand

    Purpose:
    Decides whether the server should send a command packet at the current point
    in the telemetry sequence.

    Importance of this function:
    In this project, command sending is intentionally tied to a specific telemetry
    count so that the communication flow remains predictable and testable.

    DAL reasoning:
    This is closest to DAL B because incorrect command timing may affect major
    distributed behavior and sequencing.

    Logic explanation:
    The current design sends the command only when the telemetry count reaches 2.
    */
    bool shouldSendCommand(std::uint32_t telemetryCount)
    {
        return (telemetryCount == 2U);
    }

    /*
    shouldRequestAdditionalStatus

    Purpose:
    Decides whether the server should send a DATA_REQUEST packet asking the
    aircraft client for additional status details.

    Importance of this function:
    The server uses this interaction to request extra information beyond the
    regular telemetry flow. It should happen at a controlled and predictable
    point in the session.

    DAL reasoning:
    This is closest to DAL B/C because it influences structured message flow
    between server and client.

    Logic explanation:
    The current design requests additional status only when telemetry count
    reaches 3.
    */
    bool shouldRequestAdditionalStatus(std::uint32_t telemetryCount)
    {
        return (telemetryCount == 3U);
    }

    /*
    shouldStartLargeTransfer

    Purpose:
    Decides whether the server should start the large data transfer phase.

    Importance of this function:
    Large transfer is one of the key required features of the project, but it
    must:
    - begin only at the intended telemetry stage,
    - and not be triggered more than once.

    DAL reasoning:
    This is conceptually closest to DAL B because it controls a major operational
    feature that changes server behavior and communication flow significantly.

    Logic explanation:
    The current design starts large transfer only if:
    - telemetry count has reached 5, and
    - transferTriggered is still false.

    This prevents duplicate transfer execution in the same session.
    */
    bool shouldStartLargeTransfer(std::uint32_t telemetryCount,
        bool transferTriggered)
    {
        return ((telemetryCount == 5U) && (transferTriggered == false));
    }
}