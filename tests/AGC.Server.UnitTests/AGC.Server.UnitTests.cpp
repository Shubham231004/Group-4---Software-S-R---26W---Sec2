#include "CppUnitTest.h"
#include "server_logic.hpp"

#include <vector>

/*
Purpose:
This file contains MSTest unit tests for server-specific logic used by the
ground control application.

Importance of this file:
The server is the controlling endpoint in the project. It verifies active packets,
decides when commands should be sent, requests additional status, and initiates
large data transfer.

Testing focus in this file:
- packet validation in active state
- large transfer chunk count calculation
- transfer metadata construction
- telemetry-count-based command logic
- telemetry-count-based status request logic
- large-transfer trigger logic

DAL-oriented commentary:
- DAL A relevance:
  Server-side active packet validation is conceptually one of the most critical
  safety-oriented behaviors because it determines whether inbound data should be
  trusted and acted upon.
- DAL B relevance:
  Command and transfer initiation logic can affect major operational behavior.
- DAL C relevance:
  Chunk calculations and metadata formatting strongly affect reliable large-data
  transfer.
- DAL D relevance:
  Trigger/helper logic still matters but is somewhat lower than core validation.
- DAL E relevance:
  Cosmetic-only behavior would be lower; this file mostly covers operational logic.
*/

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCServerUnitTests
{
    TEST_CLASS(ServerUnitTests)
    {
    public:
        TEST_METHOD(ValidateActivePacket_ValidPacketPasses)
        {
            /*
            DAL commentary:
            Active-packet validation is a high-value defensive control. The
            server should only operate on packets that satisfy identity,
            timestamp, and payload-size expectations.

            Closest relevance:
            DAL A / B style safety control.
            */
            const std::vector<std::uint8_t> payload{ 'O', 'K' };
            const agc::Packet packet =
                agc::makePacket(agc::MessageType::TELEMETRY, 1U, "ACFT001", payload);

            Assert::IsTrue(agc::server_logic::validateActivePacket(packet));
        }

        TEST_METHOD(ValidateActivePacket_InvalidAircraftIdFails)
        {
            /*
            DAL commentary:
            Rejecting an invalid aircraft ID on the server side is extremely
            important because it prevents untrusted or malformed data from being
            handled as legitimate input.
            */
            const std::vector<std::uint8_t> payload{ 'O', 'K' };
            const agc::Packet packet =
                agc::makePacket(agc::MessageType::TELEMETRY, 1U, "BAD001", payload);

            Assert::IsFalse(agc::server_logic::validateActivePacket(packet));
        }

        TEST_METHOD(ComputeTotalChunks_RoundsUpCorrectly)
        {
            /*
            DAL commentary:
            Chunk calculation directly affects large-transfer completeness.
            Under-counting chunks could lose data; over-counting could cause
            protocol inconsistency.

            Closest relevance:
            DAL C
            because transfer correctness depends on this logic.
            */
            const std::uint32_t total =
                agc::server_logic::computeTotalChunks(1049600U, 4096U);

            Assert::AreEqual(257U, total);
        }

        TEST_METHOD(BuildTransferStartText_FormatsExpectedMetadata)
        {
            /*
            DAL commentary:
            Transfer metadata must be accurate so the client can reconstruct and
            verify the large object correctly. This supports structured and
            reliable communication.

            Closest relevance:
            DAL B / C
            due to its role in transfer correctness.
            */
            const std::string text =
                agc::server_logic::buildTransferStartText("diagnostic_payload.bin",
                    1049600U,
                    257U,
                    999U);

            Assert::IsTrue(text.find("filename=diagnostic_payload.bin") != std::string::npos);
            Assert::IsTrue(text.find("size=1049600") != std::string::npos);
            Assert::IsTrue(text.find("chunks=257") != std::string::npos);
            Assert::IsTrue(text.find("checksum=999") != std::string::npos);
        }

        TEST_METHOD(ShouldSendCommand_OnlyOnTelemetryTwo)
        {
            /*
            DAL commentary:
            Command-trigger timing is important because sending control messages
            in the wrong phase may violate intended state/sequence behavior.

            Closest relevance:
            DAL B / C
            because command timing affects operational correctness.
            */
            Assert::IsFalse(agc::server_logic::shouldSendCommand(1U));
            Assert::IsTrue(agc::server_logic::shouldSendCommand(2U));
            Assert::IsFalse(agc::server_logic::shouldSendCommand(3U));
        }

        TEST_METHOD(ShouldRequestAdditionalStatus_OnlyOnTelemetryThree)
        {
            /*
            DAL commentary:
            The additional-status request is a controlled optional behavior and
            should occur at the intended point in the telemetry flow only.
            */
            Assert::IsFalse(agc::server_logic::shouldRequestAdditionalStatus(2U));
            Assert::IsTrue(agc::server_logic::shouldRequestAdditionalStatus(3U));
        }

        TEST_METHOD(ShouldStartLargeTransfer_RespectsTriggerAndFlag)
        {
            /*
            DAL commentary:
            Large transfer should not start too early, too late, or multiple
            times unintentionally. This is important for both reliability and
            correct state-machine behavior.
            */
            Assert::IsTrue(agc::server_logic::shouldStartLargeTransfer(5U, false));
            Assert::IsFalse(agc::server_logic::shouldStartLargeTransfer(5U, true));
            Assert::IsFalse(agc::server_logic::shouldStartLargeTransfer(4U, false));
        }

        TEST_METHOD(ComputeTotalChunks_ExactMultipleReturnsExactChunkCount)
        {
            /*
            DAL commentary:
            Boundary conditions are important in transfer logic. Exact multiples
            of chunk size must not be over-counted or under-counted.
            */
            const std::uint32_t total =
                agc::server_logic::computeTotalChunks(8192U, 4096U);

            Assert::AreEqual(2U, total);
        }

        TEST_METHOD(ShouldSendCommand_RemainsFalseOutsideTelemetryTwo)
        {
            /*
            DAL commentary:
            This reinforces that command dispatch logic remains constrained to
            the intended trigger condition only, which supports state discipline
            and predictable behavior.
            */
            Assert::IsFalse(agc::server_logic::shouldSendCommand(0U));
            Assert::IsFalse(agc::server_logic::shouldSendCommand(1U));
            Assert::IsFalse(agc::server_logic::shouldSendCommand(3U));
            Assert::IsFalse(agc::server_logic::shouldSendCommand(10U));
        }
    };
}