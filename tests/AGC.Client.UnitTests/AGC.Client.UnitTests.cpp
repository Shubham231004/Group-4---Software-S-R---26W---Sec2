#include "CppUnitTest.h"
#include "client_logic.hpp"

/*
Purpose:
This file contains MSTest unit tests for client-specific logic used by the
aircraft-side application.

Importance of this file:
The client is responsible for generating telemetry, parsing transfer metadata,
reporting transfer completion, and enforcing safe value ranges before/after
communication steps.

Testing focus in this file:
- telemetry sample generation
- telemetry progression over time
- metadata parsing for large transfer
- failed metadata parsing behavior
- transfer status text generation
- operational range validation

DAL-oriented commentary:
- DAL A relevance:
  Any client-side logic that helps prevent acceptance of malformed transfer data
  or unsafe telemetry interpretation supports high-assurance behavior.
- DAL B relevance:
  Telemetry generation and transfer metadata handling can directly affect the
  information sent to or understood from the server.
- DAL C relevance:
  Status-reporting and boundary validation are operationally significant and
  support reliable data flow and diagnostics.
- DAL D relevance:
  Some user/status messaging structures are supportive rather than primary.
- DAL E relevance:
  Purely cosmetic behavior would fall here; this file mostly tests meaningful
  behavior, not cosmetic output.
*/

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCClientUnitTests
{
    TEST_CLASS(ClientUnitTests)
    {
    public:
        TEST_METHOD(GenerateTelemetry_SampleZeroMatchesExpectedValues)
        {
            /*
            DAL commentary:
            Telemetry generation is conceptually important because the client is
            the source of aircraft state sent to the server.
            */
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(0U);

            Assert::AreEqual(30000.0, telemetry.altitudeFt, 0.01);
            Assert::AreEqual(450.0, telemetry.speedKnots, 0.01);
            Assert::AreEqual(90.0, telemetry.headingDeg, 0.01);
            Assert::AreEqual(80.0, telemetry.fuelPercent, 0.01);
        }

        TEST_METHOD(GenerateTelemetry_ProgressesAsSampleIndexIncreases)
        {
            /*
            DAL commentary:
            This test verifies deterministic telemetry progression over samples.
            Stable telemetry generation supports predictable simulation behavior
            during repeated client/server exchanges.
            */
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(4U);

            Assert::AreEqual(30400.0, telemetry.altitudeFt, 0.01);
            Assert::AreEqual(458.0, telemetry.speedKnots, 0.01);
            Assert::AreEqual(94.0, telemetry.headingDeg, 0.01);
            Assert::AreEqual(76.0, telemetry.fuelPercent, 0.01);
        }

        TEST_METHOD(ParseTransferMetadata_ValidMetadataIsAccepted)
        {
            /*
            DAL commentary:
            The large-transfer feature depends on the client correctly
            understanding metadata such as file size, chunk count, and checksum.

            Closest relevance:
            DAL B / C
            because transfer interpretation affects integrity and correctness.
            */
            const std::string metadata =
                "filename=diagnostic_payload.bin;size=1049600;chunks=257;checksum=123456";

            std::size_t expectedSize = 0U;
            std::uint32_t expectedChunks = 0U;
            std::uint32_t expectedChecksum = 0U;

            const bool ok =
                agc::client_logic::parseTransferMetadata(metadata,
                    expectedSize,
                    expectedChunks,
                    expectedChecksum);

            Assert::IsTrue(ok);
            Assert::AreEqual(static_cast<std::size_t>(1049600U), expectedSize);
            Assert::AreEqual(257U, expectedChunks);
            Assert::AreEqual(123456U, expectedChecksum);
        }

        TEST_METHOD(ParseTransferMetadata_InvalidMetadataIsRejected)
        {
            /*
            DAL commentary:
            Rejecting invalid metadata is more safety-relevant than simply
            accepting valid metadata, because malformed transfer setup data must
            not be trusted.

            Closest relevance:
            DAL A / B style defensive validation.
            */
            const std::string metadata = "filename=x.bin;size=ABC;chunks=10;checksum=100";

            std::size_t expectedSize = 0U;
            std::uint32_t expectedChunks = 0U;
            std::uint32_t expectedChecksum = 0U;

            const bool ok =
                agc::client_logic::parseTransferMetadata(metadata,
                    expectedSize,
                    expectedChunks,
                    expectedChecksum);

            Assert::IsFalse(ok);
        }

        TEST_METHOD(BuildTransferStatusText_ContainsAllFields)
        {
            /*
            DAL commentary:
            Transfer-completion status text is important for reporting and trace-
            ability. It is operationally helpful but lower critical than core
            packet or identity validation.

            Closest relevance:
            DAL C / D
            because it supports reporting and post-transfer verification.
            */
            const std::string status =
                agc::client_logic::buildTransferStatusText(true, 1049600U, 555U);

            Assert::IsTrue(status.find("TRANSFER_OK=TRUE") != std::string::npos);
            Assert::IsTrue(status.find("SIZE=1049600") != std::string::npos);
            Assert::IsTrue(status.find("CHECKSUM=555") != std::string::npos);
        }

        TEST_METHOD(IsTelemetryWithinExpectedOperationalRange_ValidTelemetryPasses)
        {
            /*
            DAL commentary:
            Range checking helps ensure unrealistic or unsafe values are not
            silently treated as valid operational telemetry.

            Closest relevance:
            DAL B / C
            because telemetry plausibility influences monitoring reliability.
            */
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(2U);

            Assert::IsTrue(agc::client_logic::isTelemetryWithinExpectedOperationalRange(telemetry));
        }

        TEST_METHOD(IsTelemetryWithinExpectedOperationalRange_InvalidTelemetryFails)
        {
            /*
            DAL commentary:
            Rejecting out-of-range values is an important defensive behavior.
            Negative altitude in this context is intentionally invalid and should
            not pass client-side operational checks.
            */
            agc::TelemetryData telemetry{};
            telemetry.altitudeFt = -1.0;
            telemetry.speedKnots = 450.0;
            telemetry.headingDeg = 90.0;
            telemetry.fuelPercent = 80.0;

            Assert::IsFalse(agc::client_logic::isTelemetryWithinExpectedOperationalRange(telemetry));
        }

        TEST_METHOD(ParseTransferMetadata_MissingChecksumIsRejected)
        {
            /*
            DAL commentary:
            Missing checksum means transfer-integrity verification cannot be
            performed properly. Rejecting such metadata supports safer transfer
            handling.

            Closest relevance:
            DAL B / C
            due to integrity implications for large-transfer processing.
            */
            const std::string metadata =
                "filename=diagnostic_payload.bin;size=1049600;chunks=257";

            std::size_t expectedSize = 0U;
            std::uint32_t expectedChunks = 0U;
            std::uint32_t expectedChecksum = 0U;

            const bool ok =
                agc::client_logic::parseTransferMetadata(metadata,
                    expectedSize,
                    expectedChunks,
                    expectedChecksum);

            Assert::IsFalse(ok);
        }

        TEST_METHOD(BuildTransferStatusText_FailedTransferContainsFalseFlag)
        {
            /*
            DAL commentary:
            The client must be able to report unsuccessful transfer completion
            accurately. This supports diagnostics, logging, and operational
            visibility after a failure scenario.
            */
            const std::string status =
                agc::client_logic::buildTransferStatusText(false, 1000U, 123U);

            Assert::IsTrue(status.find("TRANSFER_OK=FALSE") != std::string::npos);
            Assert::IsTrue(status.find("SIZE=1000") != std::string::npos);
            Assert::IsTrue(status.find("CHECKSUM=123") != std::string::npos);
        }
    };
}