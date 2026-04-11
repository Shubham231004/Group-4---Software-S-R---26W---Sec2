#include "CppUnitTest.h"
#include "common.hpp"

#include <array>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCCommonUnitTests
{
    namespace
    {
        std::array<char, agc::AIRCRAFT_ID_LENGTH> makeAircraftId(const std::string& id)
        {
            std::array<char, agc::AIRCRAFT_ID_LENGTH> result{};
            result.fill('\0');

            const std::size_t copyLength =
                (id.size() < (agc::AIRCRAFT_ID_LENGTH - 1U)) ? id.size() : (agc::AIRCRAFT_ID_LENGTH - 1U);

            for (std::size_t index = 0U; index < copyLength; ++index)
            {
                result[index] = id[index];
            }

            return result;
        }
    }

    TEST_CLASS(CommonUnitTests)
    {
    public:
        TEST_METHOD(ValidateAircraftId_AcceptsConfiguredId)
        {
            const auto id = makeAircraftId("ACFT001");
            Assert::IsTrue(agc::validateAircraftId(id));
        }

        TEST_METHOD(ValidateAircraftId_RejectsUnexpectedId)
        {
            const auto id = makeAircraftId("BAD001");
            Assert::IsFalse(agc::validateAircraftId(id));
        }

        TEST_METHOD(ValidatePayloadSize_RejectsOversizePayload)
        {
            Assert::IsFalse(agc::validatePayloadSize(agc::MAX_PAYLOAD_SIZE + 1U));
        }

        TEST_METHOD(ValidateTimestamp_RejectsPacketOutsideAllowedWindow)
        {
            const std::uint64_t now = agc::getCurrentTimeMs();
            const std::uint64_t stale = now - (agc::MAX_TIMESTAMP_DRIFT_MS + 1000ULL);
            Assert::IsFalse(agc::validateTimestamp(stale, now));
        }

        TEST_METHOD(TelemetryPayload_RoundTripPreservesValues)
        {
            agc::TelemetryData expected{};
            expected.altitudeFt = 30000.0;
            expected.speedKnots = 450.0;
            expected.headingDeg = 90.0;
            expected.fuelPercent = 80.0;

            const auto payload = agc::telemetryToPayload(expected);

            agc::TelemetryData actual{};
            const bool ok = agc::payloadToTelemetry(payload, actual);

            Assert::IsTrue(ok);
            Assert::AreEqual(expected.altitudeFt, actual.altitudeFt, 0.01);
            Assert::AreEqual(expected.speedKnots, actual.speedKnots, 0.01);
            Assert::AreEqual(expected.headingDeg, actual.headingDeg, 0.01);
            Assert::AreEqual(expected.fuelPercent, actual.fuelPercent, 0.01);
        }

        TEST_METHOD(MakePacket_SetsExpectedHeaderFields)
        {
            const std::vector<std::uint8_t> payload{ 'A', 'B', 'C' };
            const agc::Packet packet =
                agc::makePacket(agc::MessageType::TELEMETRY, 7U, "ACFT001", payload);

            Assert::AreEqual(static_cast<std::uint32_t>(agc::MAGIC_NUMBER), packet.header.magic);
            Assert::AreEqual(static_cast<std::uint16_t>(agc::PROTOCOL_VERSION), packet.header.version);
            Assert::AreEqual(static_cast<std::uint32_t>(agc::MessageType::TELEMETRY), packet.header.messageType);
            Assert::AreEqual(7U, packet.header.sequenceNumber);
            Assert::AreEqual(3U, packet.header.payloadSize);
            Assert::IsTrue(agc::safeAircraftIdToString(packet.header.aircraftId) == "ACFT001");
        }

        TEST_METHOD(ComputeChecksum_IsStableForKnownInput)
        {
            const std::vector<std::uint8_t> data{ 1U, 2U, 3U, 4U, 5U };
            const std::uint32_t checksum = agc::computeChecksum(data);
            Assert::AreEqual(15U, checksum);
        }
    };
}