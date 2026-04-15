#ifndef AGC_SERVER_LOGIC_HPP
#define AGC_SERVER_LOGIC_HPP

/*
Project  : Aircraft-Ground Control Communication System
Course   : CSCN74000 - Software Safety & Reliability
Group 4  : Shubham, Yinus, Brian
File     : server_logic.hpp

Purpose of this file:
This header declares server-side helper logic used by the ground control
application. These functions are separated from server_main.cpp so they can be:
- reused cleanly by the server,
- tested independently using MSTest,
- and documented as server-specific operational logic rather than low-level
  socket handling.

Main responsibilities declared in this file:
- validating whether an incoming packet is acceptable in the active session,
- computing how many chunks are required for a large file transfer,
- building the metadata string for the start of a large transfer,
- deciding when command processing should occur,
- deciding when additional status should be requested,
- deciding when a large transfer should begin.

Importance of this file:
The server is the controlling endpoint in the project. It verifies what it
receives, decides when to send commands, determines when additional aircraft
status should be requested, and initiates the required 1 MB+ data transfer.
If these decisions are wrong, the state machine and message flow can become
inconsistent or unsafe.

DAL-oriented commentary:
- DAL A style relevance:
  Active-packet validation acts as a defensive gatekeeper before the server
  trusts and processes inbound communication.
- DAL B style relevance:
  Command timing, status-request timing, and large-transfer initiation all
  influence major server behavior and communication sequencing.
- DAL C style relevance:
  Chunk-count calculation and transfer-start metadata generation support
  reliable execution of the large-transfer feature.
- DAL D/E style relevance:
  This file mainly declares operationally meaningful helpers rather than
  cosmetic utilities, so most declarations are more significant than DAL D/E.
*/

#include "common.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace agc::server_logic
{
    /*
    validateActivePacket

    Purpose:
    Validates that an incoming packet is acceptable for processing while the
    server is in an active communication session.

    Importance of this function:
    During the ACTIVE phase, the server should not process arbitrary data.
    It should only process packets that pass key validation checks such as:
    - valid aircraft identity,
    - acceptable timestamp window,
    - valid payload size.

    Logic expectation:
    The function performs a combined validation check on the packet header fields
    most relevant to runtime trust and protocol safety.

    Safety / reliability reasoning:
    This function acts as a defensive filter before the server processes packet
    content during active operation.

    DAL reasoning:
    This is conceptually closest to DAL A/B because it protects the server from
    accepting malformed, stale, or unauthorized active-session packets.

    Parameter:
    - packet:
      The incoming packet to validate.

    Return:
    - true  -> packet passes active-session checks
    - false -> packet is invalid for active-session processing
    */
    bool validateActivePacket(const Packet& packet);

    /*
    computeTotalChunks

    Purpose:
    Calculates how many data chunks are required to transmit a large payload
    using the configured chunk size.

    Importance of this function:
    The project requires transfer of a large object (>1 MB), but the system
    must still use bounded packet sizes. Therefore, the payload must be split
    into smaller chunks.

    Logic expectation:
    - if chunk size is zero, the function returns zero to avoid division by zero
    - otherwise, it rounds up so partial final chunks are counted properly

    Safety / reliability reasoning:
    Incorrect chunk calculation could cause:
    - missing data,
    - extra unexpected chunks,
    - transfer reconstruction failure at the client side.

    DAL reasoning:
    This is conceptually closest to DAL C because it directly affects reliable
    execution of the large-transfer requirement.

    Parameters:
    - totalSize:
      Total number of bytes that must be transferred.
    - chunkSize:
      Maximum number of bytes per chunk.

    Return:
    - Total number of chunks required for the transfer.
    */
    std::uint32_t computeTotalChunks(std::size_t totalSize,
        std::size_t chunkSize);

    /*
    buildTransferStartText

    Purpose:
    Builds the metadata string placed into the LARGE_DATA_START packet before
    chunk transmission begins.

    Importance of this function:
    The client needs transfer metadata before it can correctly reconstruct the
    large payload. This metadata includes:
    - file name,
    - total size,
    - total chunk count,
    - checksum.

    Logic expectation:
    The function formats these values into a single structured text string that
    the client can later parse using its metadata parser.

    Safety / reliability reasoning:
    Transfer metadata must be internally consistent so the client can validate
    received data against expected values.

    DAL reasoning:
    This is closest to DAL B/C because it supports structured and reliable
    execution of a major system-level transfer feature.

    Parameters:
    - fileName:
      The logical file name associated with the transfer.
    - totalSize:
      The full payload size in bytes.
    - totalChunks:
      The number of chunks the server will send.
    - checksum:
      The checksum calculated over the large payload.

    Return:
    - A formatted metadata string for the LARGE_DATA_START packet.
    */
    std::string buildTransferStartText(const std::string& fileName,
        std::size_t totalSize,
        std::uint32_t totalChunks,
        std::uint32_t checksum);

    /*
    shouldSendCommand

    Purpose:
    Determines whether the server should send a command message based on the
    current telemetry count.

    Importance of this function:
    In this project, command sending is intentionally tied to a specific point
    in the telemetry flow. This keeps the sequence predictable and testable.

    Logic expectation:
    The function returns true only when the telemetry count reaches the intended
    trigger point for command dispatch.

    Safety / reliability reasoning:
    Triggering commands at the wrong time could cause inconsistent behavior or
    violate expected sequencing in the active session.

    DAL reasoning:
    This is conceptually closest to DAL B because command timing influences major
    operational behavior of the server.

    Parameter:
    - telemetryCount:
      The number of telemetry packets successfully processed so far.

    Return:
    - true  -> command should be sent now
    - false -> no command should be sent at this time
    */
    bool shouldSendCommand(std::uint32_t telemetryCount);

    /*
    shouldRequestAdditionalStatus

    Purpose:
    Determines whether the server should send a DATA_REQUEST message to obtain
    additional status information from the aircraft client.

    Importance of this function:
    The project includes an extra status-request interaction after telemetry has
    reached a defined point in the session.

    Logic expectation:
    The function returns true only at the intended telemetry count for requesting
    additional aircraft status.

    Safety / reliability reasoning:
    Requesting status too early, too often, or at the wrong phase could disrupt
    the intended communication sequence.

    DAL reasoning:
    This is conceptually closest to DAL B/C because it influences operational
    message flow and distributed-session behavior.

    Parameter:
    - telemetryCount:
      The number of telemetry packets processed by the server so far.

    Return:
    - true  -> additional status should be requested now
    - false -> no status request should be made at this time
    */
    bool shouldRequestAdditionalStatus(std::uint32_t telemetryCount);

    /*
    shouldStartLargeTransfer

    Purpose:
    Determines whether the server should initiate the large data transfer.

    Importance of this function:
    The project requires a command-initiated large transfer, but the server must
    ensure that:
    - it begins only at the intended phase of the session,
    - and it is not triggered multiple times accidentally.

    Logic expectation:
    The function checks:
    - whether telemetry count has reached the trigger point,
    - and whether transfer has already been triggered before.

    Safety / reliability reasoning:
    Re-triggering large transfer unintentionally or starting it at the wrong
    time could disrupt the session state machine and data-flow consistency.

    DAL reasoning:
    This is conceptually closest to DAL B because it controls a major required
    system feature and affects the server’s operational sequence directly.

    Parameters:
    - telemetryCount:
      The number of telemetry packets processed so far.
    - transferTriggered:
      Flag indicating whether the transfer has already been started previously.

    Return:
    - true  -> large transfer should begin now
    - false -> large transfer should not begin at this time
    */
    bool shouldStartLargeTransfer(std::uint32_t telemetryCount,
        bool transferTriggered);
}

#endif