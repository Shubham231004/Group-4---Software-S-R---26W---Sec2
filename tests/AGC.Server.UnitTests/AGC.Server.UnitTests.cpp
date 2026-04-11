#include "CppUnitTest.h"
#include "server_logic.hpp"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCServerUnitTests
{
    TEST_CLASS(ServerUnitTests)
    {
    public:
        TEST_METHOD(ValidateActivePacket_ValidPacketPasses)
        {
            const std::vector<std::uint8_t> payload{ 'O', 'K' };
            const agc::Packet packet =
                agc::makePacket(agc::MessageType::TELEMETRY, 1U, "ACFT001", payload);

            Assert::IsTrue(agc::server_logic::validateActivePacket(packet));
        }

        TEST_METHOD(ValidateActivePacket_InvalidAircraftIdFails)
        {
            const std::vector<std::uint8_t> payload{ 'O', 'K' };
            const agc::Packet packet =
                agc::makePacket(agc::MessageType::TELEMETRY, 1U, "BAD001", payload);

            Assert::IsFalse(agc::server_logic::validateActivePacket(packet));
        }

        TEST_METHOD(ComputeTotalChunks_RoundsUpCorrectly)
        {
            const std::uint32_t total =
                agc::server_logic::computeTotalChunks(1049600U, 4096U);

            Assert::AreEqual(257U, total);
        }

        TEST_METHOD(BuildTransferStartText_FormatsExpectedMetadata)
        {
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
            Assert::IsFalse(agc::server_logic::shouldSendCommand(1U));
            Assert::IsTrue(agc::server_logic::shouldSendCommand(2U));
            Assert::IsFalse(agc::server_logic::shouldSendCommand(3U));
        }

        TEST_METHOD(ShouldRequestAdditionalStatus_OnlyOnTelemetryThree)
        {
            Assert::IsFalse(agc::server_logic::shouldRequestAdditionalStatus(2U));
            Assert::IsTrue(agc::server_logic::shouldRequestAdditionalStatus(3U));
        }

        TEST_METHOD(ShouldStartLargeTransfer_RespectsTriggerAndFlag)
        {
            Assert::IsTrue(agc::server_logic::shouldStartLargeTransfer(5U, false));
            Assert::IsFalse(agc::server_logic::shouldStartLargeTransfer(5U, true));
            Assert::IsFalse(agc::server_logic::shouldStartLargeTransfer(4U, false));
        }
    };
}