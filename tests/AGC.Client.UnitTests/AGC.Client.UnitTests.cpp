#include "CppUnitTest.h"
#include "client_logic.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AGCClientUnitTests
{
    TEST_CLASS(ClientUnitTests)
    {
    public:
        TEST_METHOD(GenerateTelemetry_SampleZeroMatchesExpectedValues)
        {
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(0U);

            Assert::AreEqual(30000.0, telemetry.altitudeFt, 0.01);
            Assert::AreEqual(450.0, telemetry.speedKnots, 0.01);
            Assert::AreEqual(90.0, telemetry.headingDeg, 0.01);
            Assert::AreEqual(80.0, telemetry.fuelPercent, 0.01);
        }

        TEST_METHOD(GenerateTelemetry_ProgressesAsSampleIndexIncreases)
        {
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(4U);

            Assert::AreEqual(30400.0, telemetry.altitudeFt, 0.01);
            Assert::AreEqual(458.0, telemetry.speedKnots, 0.01);
            Assert::AreEqual(94.0, telemetry.headingDeg, 0.01);
            Assert::AreEqual(76.0, telemetry.fuelPercent, 0.01);
        }

        TEST_METHOD(ParseTransferMetadata_ValidMetadataIsAccepted)
        {
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
            const std::string status =
                agc::client_logic::buildTransferStatusText(true, 1049600U, 555U);

            Assert::IsTrue(status.find("TRANSFER_OK=TRUE") != std::string::npos);
            Assert::IsTrue(status.find("SIZE=1049600") != std::string::npos);
            Assert::IsTrue(status.find("CHECKSUM=555") != std::string::npos);
        }

        TEST_METHOD(IsTelemetryWithinExpectedOperationalRange_ValidTelemetryPasses)
        {
            const agc::TelemetryData telemetry =
                agc::client_logic::generateTelemetry(2U);

            Assert::IsTrue(agc::client_logic::isTelemetryWithinExpectedOperationalRange(telemetry));
        }

        TEST_METHOD(IsTelemetryWithinExpectedOperationalRange_InvalidTelemetryFails)
        {
            agc::TelemetryData telemetry{};
            telemetry.altitudeFt = -1.0;
            telemetry.speedKnots = 450.0;
            telemetry.headingDeg = 90.0;
            telemetry.fuelPercent = 80.0;

            Assert::IsFalse(agc::client_logic::isTelemetryWithinExpectedOperationalRange(telemetry));
        }
    };
}