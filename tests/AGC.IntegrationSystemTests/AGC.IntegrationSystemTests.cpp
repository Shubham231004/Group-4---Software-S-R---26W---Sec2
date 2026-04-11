#include "CppUnitTest.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace AGCIntegrationSystemTests
{
    namespace
    {
        std::wstring toWide(const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        }

        fs::path findRepoRoot()
        {
            fs::path current = fs::current_path();

            for (int depth = 0; depth < 8; ++depth)
            {
                if (fs::exists(current / "include" / "common.hpp"))
                {
                    return current;
                }

                if (current.has_parent_path() == false)
                {
                    break;
                }

                current = current.parent_path();
            }

            Assert::Fail(L"Could not locate repository root.");
            return fs::current_path();
        }

        fs::path findBuiltExecutable(const fs::path& repoRoot, const std::wstring& exeName)
        {
            const std::vector<fs::path> candidates =
            {
                repoRoot / "build" / "Debug" / exeName,
                repoRoot / "build" / exeName,
                repoRoot / "build" / "Release" / exeName
            };

            for (const auto& path : candidates)
            {
                if (fs::exists(path))
                {
                    return path;
                }
            }

            Assert::Fail((L"Executable not found: " + exeName).c_str());
            return fs::path{};
        }

        PROCESS_INFORMATION startProcess(const fs::path& exePath)
        {
            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);

            PROCESS_INFORMATION processInfo{};
            std::wstring commandLine = L"\"" + exePath.wstring() + L"\"";
            std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
            buffer.push_back(L'\0');

            const BOOL ok = CreateProcessW(
                nullptr,
                buffer.data(),
                nullptr,
                nullptr,
                FALSE,
                0U,
                nullptr,
                exePath.parent_path().wstring().c_str(),
                &startupInfo,
                &processInfo);

            if (ok == FALSE)
            {
                Assert::Fail((L"Failed to start process: " + exePath.wstring()).c_str());
            }

            return processInfo;
        }

        DWORD waitForProcess(PROCESS_INFORMATION& processInfo, DWORD timeoutMs)
        {
            const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMs);
            if (waitResult != WAIT_OBJECT_0)
            {
                TerminateProcess(processInfo.hProcess, 1U);
                Assert::Fail(L"Process timed out.");
            }

            DWORD exitCode = 1U;
            GetExitCodeProcess(processInfo.hProcess, &exitCode);

            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);

            return exitCode;
        }

        std::string readAllText(const fs::path& path)
        {
            std::ifstream input(path);
            std::ostringstream buffer{};
            buffer << input.rdbuf();
            return buffer.str();
        }

        void deleteIfExists(const fs::path& path)
        {
            if (fs::exists(path))
            {
                fs::remove(path);
            }
        }
    }

    TEST_CLASS(IntegrationSystemTests)
    {
    public:
        TEST_METHOD(EndToEnd_ClientAndServerCompleteSuccessfully)
        {
            const fs::path repoRoot = findRepoRoot();
            const fs::path serverExe = findBuiltExecutable(repoRoot, L"server.exe");
            const fs::path clientExe = findBuiltExecutable(repoRoot, L"client.exe");

            deleteIfExists(repoRoot / "server_log.csv");
            deleteIfExists(repoRoot / "client_log.csv");
            deleteIfExists(repoRoot / "received_diagnostic_payload.bin");

            PROCESS_INFORMATION serverProcess = startProcess(serverExe);
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));

            PROCESS_INFORMATION clientProcess = startProcess(clientExe);

            const DWORD clientExit = waitForProcess(clientProcess, 60000U);
            const DWORD serverExit = waitForProcess(serverProcess, 60000U);

            Assert::AreEqual(static_cast<DWORD>(0U), clientExit);
            Assert::AreEqual(static_cast<DWORD>(0U), serverExit);
        }

        TEST_METHOD(Integration_LogsAreGeneratedAndContainExpectedEvents)
        {
            const fs::path repoRoot = findRepoRoot();
            const fs::path clientLog = repoRoot / "client_log.csv";
            const fs::path serverLog = repoRoot / "server_log.csv";

            Assert::IsTrue(fs::exists(clientLog), L"client_log.csv was not generated.");
            Assert::IsTrue(fs::exists(serverLog), L"server_log.csv was not generated.");

            const std::string clientText = readAllText(clientLog);
            const std::string serverText = readAllText(serverLog);

            Assert::IsTrue(clientText.find("CONNECT_ACK") != std::string::npos);
            Assert::IsTrue(clientText.find("TELEMETRY_ACK") != std::string::npos);
            Assert::IsTrue(serverText.find("CONNECT_REQUEST") != std::string::npos);
            Assert::IsTrue(serverText.find("LARGE_DATA_START") != std::string::npos);
            Assert::IsTrue(serverText.find("LARGE_DATA_END") != std::string::npos);
        }

        TEST_METHOD(System_LargeTransferProducesReceivedFileAboveOneMegabyte)
        {
            const fs::path repoRoot = findRepoRoot();
            const fs::path receivedFile = repoRoot / "received_diagnostic_payload.bin";

            Assert::IsTrue(fs::exists(receivedFile), L"Received file was not created.");

            const auto fileSize = fs::file_size(receivedFile);
            Assert::IsTrue(fileSize >= 1049600ULL, L"Received file is smaller than required.");
        }
    };
}