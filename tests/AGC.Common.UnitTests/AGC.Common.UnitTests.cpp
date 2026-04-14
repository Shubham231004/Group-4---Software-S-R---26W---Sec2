#include "CppUnitTest.h"
#include "common.hpp"

#include <array>
#include <string>
#include <vector>

/*
Purpose:
This file contains MSTest unit tests for shared/common protocol logic used by
both the aircraft client and ground control server.

Importance of this file:
The "common" module is the shared foundation of the entire distributed system.
If common packet, validation, serialization, or checksum logic is wrong, both
applications may fail together or behave inconsistently.

Testing focus in this file:
- aircraft ID validation
- payload size validation
- timestamp validation
- telemetry payload conversion
- packet creation correctness
- checksum correctness
- malformed payload rejection
- large diagnostic blob sizing

DAL-oriented :
- DAL A relevance:
  Validation logic that prevents corrupted, malformed, or unsafe data from being
  accepted can be considered closest to DAL A style criticality in concept,
  because failure here could allow unsafe system behavior to propagate.
- DAL B relevance:
  Packet construction and telemetry round-trip correctness are highly important
  because wrong packet structure can cause serious miscommunication between
  aircraft and ground.
- DAL C relevance:
  Checksum and blob-sizing logic strongly affect operational reliability and
  data integrity, especially for large transfer functions.
- DAL D relevance:
  Some formatting or supporting utility checks are lower criticality than core
  validation logic, but still necessary for robust distributed behavior.
- DAL E relevance:
  Purely descriptive or cosmetic behavior would fall closer to DAL E; however,
  this particular file mostly tests non-cosmetic core logic.
*/

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCCommonUnitTests
{
    namespace
    {
        /*
        Helper: makeAircraftId
        Purpose:
        Creates a packet-style fixed-length aircraft ID array so tests can call
        the same validation logic used in the production code.

        Safety relevance:
        Aircraft identity is a foundational trust/control input. If identity
        validation fails, unauthorized or incorrect endpoints may participate in
        communication.

        DAL rationale:
        - Closest conceptual relevance: DAL A / B
        - Reason: endpoint identity validation can affect whether critical
          commands/results are accepted into the system.
        */
        std::array<char, agc::AIRCRAFT_ID_LENGTH> makeAircraftId(const std::string& id)
        {
            std::array<char, agc::AIRCRAFT_ID_LENGTH> result{};
            result.fill('\0');

            const std::size_t copyLength =
                (id.size() < (agc::AIRCRAFT_ID_LENGTH - 1U))
                ? id.size()
                : (agc::AIRCRAFT_ID_LENGTH - 1U);

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
            /*
            DAL commentary:
            This test supports identity validation, which is conceptually high
            criticality because it helps ensure the system communicates only with
            the expected aircraft endpoint.
            */
            const auto id = makeAircraftId("ACFT001");
            Assert::IsTrue(agc::validateAircraftId(id));
        }

        TEST_METHOD(ValidateAircraftId_RejectsUnexpectedId)
        {
            /*
            DAL commentary:
            Rejecting an unexpected aircraft ID is as important as accepting the
            correct one, because unsafe acceptance is often more dangerous than
            false rejection in safety-oriented systems.
            */
            const auto id = makeAircraftId("BAD001");
            Assert::IsFalse(agc::validateAircraftId(id));
        }

        TEST_METHOD(ValidatePayloadSize_RejectsOversizePayload)
        {
            /*
            DAL commentary:
            Payload bounding protects the system from malformed or excessive
            packet sizes. In safety-conscious systems, bounds checking is a
            highly important defensive measure.

            Closest conceptual relevance:
            DAL A / B style defensive programming concern.
            */
            Assert::IsFalse(agc::validatePayloadSize(agc::MAX_PAYLOAD_SIZE + 1U));
        }

        TEST_METHOD(ValidateTimestamp_RejectsPacketOutsideAllowedWindow)
        {
            /*
            DAL commentary:
            Timestamp validation helps reject stale or abnormal packets that
            might otherwise be processed out of context.

            Importance:
            A delayed or replayed packet could cause the server or client to
            act on outdated information.
            */
            const std::uint64_t now = agc::getCurrentTimeMs();
            const std::uint64_t stale = now - (agc::MAX_TIMESTAMP_DRIFT_MS + 1000ULL);

            Assert::IsFalse(agc::validateTimestamp(stale, now));
        }

        TEST_METHOD(TelemetryPayload_RoundTripPreservesValues)
        {
            /*
            DAL commentary:
            Telemetry integrity is central to the project. If telemetry values
            are not preserved correctly through serialization/deserialization,
            monitoring decisions may be based on wrong data.

            Conceptual relevance:
            DAL B / C
            because incorrect telemetry may cause major but not necessarily
            catastrophic behavior in this course-project simulation.
            */
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
            /*
            DAL commentary:
            Correct packet header population is required for predictable protocol
            behavior. Message type, sequence number, aircraft ID, and payload
            size are all structural fields that affect communication safety.

            Closest relevance:
            DAL B / C
            because incorrect packet structure can break distributed behavior.
            */
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
            /*
            DAL commentary:
            Checksum logic supports transfer integrity verification. This is
            especially important for the large diagnostic payload transferred
            from server to client.

            Conceptual relevance:
            DAL C
            because checksum mismatch would undermine data integrity but is
            somewhat secondary to primary command/identity validation.
            */
            const std::vector<std::uint8_t> data{ 1U, 2U, 3U, 4U, 5U };
            const std::uint32_t checksum = agc::computeChecksum(data);

            Assert::AreEqual(15U, checksum);
        }

        TEST_METHOD(TelemetryPayload_MalformedPayloadIsRejected)
        {
            /*
            DAL commentary:
            Rejecting malformed payload content is an important robustness and
            safety behavior. Unsafe parsing should never interpret invalid data
            as valid telemetry.

            Closest relevance:
            DAL A / B style input validation concern.
            */
            const std::vector<std::uint8_t> malformedPayload{ 'B', 'A', 'D', '_', 'D', 'A', 'T', 'A' };
            agc::TelemetryData telemetry{};

            const bool ok = agc::payloadToTelemetry(malformedPayload, telemetry);

            Assert::IsFalse(ok);
        }

        TEST_METHOD(GenerateDiagnosticBlob_ReturnsRequestedSize)
        {
            /*
            DAL commentary:
            This supports the large-transfer feature. It is not as safety-
            critical as identity or packet validation, but it is still important
            for verifying the 1 MB+ system requirement.

            Closest relevance:
            DAL C / D
            because it affects transfer correctness and requirement compliance.
            */
            const std::size_t requestedSize = 1049600U;
            const std::vector<std::uint8_t> blob = agc::generateDiagnosticBlob(requestedSize);

            Assert::AreEqual(requestedSize, blob.size());
        }
    };
}