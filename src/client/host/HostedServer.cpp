/// @file HostedServer.cpp
/// @brief Platform-specific process management for locally hosted servers.

#include "HostedServer.hpp"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <string>
#include <vector>

namespace
{
constexpr int k_shutdownPollMs = 25;
constexpr int k_shutdownTimeoutMs = 1000;

std::string getServerBinaryPath()
{
    const char* basePath = SDL_GetBasePath();
    if (!basePath) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get base path: %s", SDL_GetError());
        return {};
    }

    std::string path = basePath;

#if defined(_WIN32)
    path += "server.exe";
#else
    path += "server";
#endif

    return path;
}

std::vector<std::string> buildServerArgs(const std::string& serverPath, const HostConfigState& config)
{
    std::vector<std::string> args;
    args.push_back(serverPath);
    args.emplace_back("--address=0.0.0.0");
    const int requestedPort = config.useSpecificPort ? config.port : 0;
    args.push_back("--port=" + std::to_string(requestedPort));
    if (config.useLegacyTcp) {
        args.emplace_back("--legacy-tcp");
    }

    args.push_back("--killsToWin=" + std::to_string(config.killsToWin));
    args.emplace_back("--idle-shutdown-minutes=5");
    return args;
}

bool consumeReadyPort(std::string& output, uint16_t& outPort)
{
    const size_t newline = output.find('\n');
    if (newline == std::string::npos)
        return false;

    std::string line = output.substr(0, newline);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    output.erase(0, newline + 1);

    if (line.rfind("READY ", 0) != 0)
        return false;

    char* end = nullptr;
    const long port = std::strtol(line.c_str() + 6, &end, 10);
    if (end == line.c_str() + 6 || *end != '\0' || port < 0 || port > 65535)
        return false;

    outPort = static_cast<uint16_t>(port);
    return true;
}

#if defined(_WIN32)
std::string windowsErrorString(DWORD error)
{
    char* message = nullptr;
    const DWORD len =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr,
                       error,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&message),
                       0,
                       nullptr);
    std::string result = len > 0 && message != nullptr ? std::string(message, len) : "unknown error";
    if (message != nullptr) {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

std::string quoteWindowsArg(const std::string& arg)
{
    if (arg.empty() || arg.find_first_of(" \t\n\v\"") != std::string::npos) {
        std::string quoted = "\"";
        unsigned int backslashes = 0;
        for (const char c : arg) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            if (c == '"') {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back('"');
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(c);
        }
        quoted.append(backslashes * 2, '\\');
        quoted.push_back('"');
        return quoted;
    }
    return arg;
}

std::string buildWindowsCommandLine(const std::vector<std::string>& args)
{
    std::string commandLine;
    for (const auto& arg : args) {
        if (!commandLine.empty()) {
            commandLine.push_back(' ');
        }
        commandLine += quoteWindowsArg(arg);
    }
    return commandLine;
}
#endif
} // namespace

bool HostedServer::start(HostConfigState const& config, std::string& outError)
{
    if (isRunning()) {
        outError = "Hosted server is already running";
        return false;
    }

    SDL_Log("Starting hosted server...");

    std::string serverPath = getServerBinaryPath();
    if (serverPath.empty()) {
        outError = "Could not locate server executable";
        return false;
    }

    std::vector<std::string> args = buildServerArgs(serverPath, config);

#if defined(_WIN32)
    SECURITY_ATTRIBUTES pipeAttrs{};
    pipeAttrs.nLength = sizeof(pipeAttrs);
    pipeAttrs.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &pipeAttrs, 0)) {
        outError = "Failed to create pipe for server stdout: " + windowsErrorString(GetLastError());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        outError = "Failed to configure server stdout pipe: " + windowsErrorString(GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (startup.hStdInput == INVALID_HANDLE_VALUE) {
        startup.hStdInput = nullptr;
    }
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stdoutWrite;

    PROCESS_INFORMATION process{};
    std::string commandLine = buildWindowsCommandLine(args);
    if (!CreateProcessA(serverPath.c_str(),
                        commandLine.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        config.persistAfterClientExit ? CREATE_NEW_PROCESS_GROUP : 0,
                        nullptr,
                        nullptr,
                        &startup,
                        &process))
    {
        outError = "Failed to create server process: " + windowsErrorString(GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    CloseHandle(stdoutWrite);
    CloseHandle(process.hThread);
    childProcess = process.hProcess;
    childPid = process.dwProcessId;

    std::string output;
    char buffer[256];
    while (true) {
        DWORD bytesRead = 0;
        if (ReadFile(stdoutRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            output.append(buffer, bytesRead);
            if (consumeReadyPort(output, boundPort)) {
                CloseHandle(stdoutRead);
                SDL_Log("Hosted server process started with PID %lu at port %u",
                        static_cast<unsigned long>(childPid),
                        static_cast<unsigned>(boundPort));
                if (config.persistAfterClientExit) {
                    detachForPersistence();
                }
                return true;
            }
            continue;
        }

        const DWORD readError = bytesRead == 0 ? ERROR_BROKEN_PIPE : GetLastError();
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(static_cast<HANDLE>(childProcess), &exitCode);
        if (exitCode != STILL_ACTIVE || readError == ERROR_BROKEN_PIPE) {
            outError = "Server exited before reporting readiness";
        } else {
            outError = "Failed to read server stdout: " + windowsErrorString(readError);
        }
        CloseHandle(stdoutRead);
        CloseHandle(static_cast<HANDLE>(childProcess));
        childProcess = nullptr;
        childPid = 0;
        boundPort = 0;
        return false;
    }
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    int stdoutPipe[2];
    if (pipe(stdoutPipe) != 0) {
        outError = std::string("Failed to create pipe for server stdout: ") + std::strerror(errno);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    pid_t pid = fork();

    if (pid < 0) {
        outError = std::string("Failed to fork server process: ") + std::strerror(errno);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    if (pid == 0) {
        // Setup child process to write its stdout to the pipe, then exec the server binary
        close(stdoutPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdoutPipe[1]);
        if (config.persistAfterClientExit) {
            if (setsid() < 0) {
                _exit(127);
            }

            const pid_t grandchild = fork();
            if (grandchild < 0) {
                _exit(127);
            }
            if (grandchild > 0) {
                _exit(0);
            }
        }

        execv(serverPath.c_str(), argv.data());

        _exit(127); // exec failed
    }

    // Close write end of pipe
    close(stdoutPipe[1]);

    childPid = pid;

    std::string output;
    char buffer[256];

    while (true) {
        ssize_t n = read(stdoutPipe[0], buffer, sizeof(buffer));
        if (n > 0) {
            output.append(buffer, static_cast<size_t>(n));
            if (consumeReadyPort(output, boundPort)) {
                close(stdoutPipe[0]);
                SDL_Log("Hosted server process started with PID %d at port %u", pid, static_cast<unsigned>(boundPort));
                if (config.persistAfterClientExit) {
                    waitpid(childPid, nullptr, 0);
                    detachForPersistence();
                }
                return true;
            }
        }

        if (n == 0) {
            outError = "Server exited before reporting readiness";
            close(stdoutPipe[0]);
            waitpid(childPid, nullptr, WNOHANG);
            childPid = -1;
            boundPort = 0;
            return false;
        }

        if (n < 0) {
            outError = "Failed to read server stdout";
            close(stdoutPipe[0]);
            kill(childPid, SIGTERM);
            waitpid(childPid, nullptr, WNOHANG);
            childPid = -1;
            boundPort = 0;
            return false;
        }
    }
#endif
}

void HostedServer::detachForPersistence()
{
#if defined(_WIN32)
    if (childProcess != nullptr) {
        CloseHandle(static_cast<HANDLE>(childProcess));
        childProcess = nullptr;
    }
    childPid = 0;
#else
    childPid = -1;
#endif
    SDL_Log("Hosted server detached for persistence");
}

void HostedServer::shutdown()
{
    SDL_Log("Shutting down hosted server...");
#if defined(_WIN32)
    if (childProcess == nullptr) {
        return;
    }

    HANDLE process = static_cast<HANDLE>(childProcess);
    const DWORD waitResult = WaitForSingleObject(process, 0);
    if (waitResult == WAIT_OBJECT_0) {
        CloseHandle(process);
        childProcess = nullptr;
        childPid = 0;
        boundPort = 0;
        return;
    }

    if (!TerminateProcess(process, 0)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to terminate hosted server process: %s",
                    windowsErrorString(GetLastError()).c_str());
    }
    WaitForSingleObject(process, k_shutdownTimeoutMs);
    CloseHandle(process);
    childProcess = nullptr;
    childPid = 0;
    boundPort = 0;
    SDL_Log("Hosted server process terminated");
#else
    if (childPid <= 0) {
        return;
    }

    if (kill(childPid, SIGTERM) != 0 && errno == ESRCH) {
        childPid = -1;
        boundPort = 0;
        return;
    }

    int status = 0;
    for (int waitedMs = 0; waitedMs < k_shutdownTimeoutMs; waitedMs += k_shutdownPollMs) {
        const pid_t result = waitpid(childPid, &status, WNOHANG);
        if (result == childPid) {
            childPid = -1;
            boundPort = 0;
            SDL_Log("Hosted server process terminated");
            return;
        }
        if (result < 0 && errno == ECHILD) {
            childPid = -1;
            boundPort = 0;
            return;
        }
        SDL_Delay(k_shutdownPollMs);
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Hosted server did not exit after SIGTERM; forcing shutdown");
    kill(childPid, SIGKILL);
    waitpid(childPid, &status, 0);
    childPid = -1;
    boundPort = 0;
    SDL_Log("Hosted server process force-terminated");
#endif
}

bool HostedServer::isRunning()
{
#if defined(_WIN32)
    if (childProcess == nullptr) {
        return false;
    }

    HANDLE process = static_cast<HANDLE>(childProcess);
    const DWORD waitResult = WaitForSingleObject(process, 0);
    if (waitResult == WAIT_TIMEOUT) {
        return true;
    }

    CloseHandle(process);
    childProcess = nullptr;
    childPid = 0;
    boundPort = 0;
    return false;
#else
    if (childPid <= 0) {
        return false;
    }

    int status = 0;
    const pid_t result = waitpid(childPid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }

    if (result == childPid || (result < 0 && errno == ECHILD)) {
        childPid = -1;
        boundPort = 0;
    }
    return false;
#endif
}

uint16_t HostedServer::port()
{
    return boundPort;
}

bool HostedServer::hasSession() const
{
    return boundPort != 0;
}

void HostedServer::clearSession()
{
    boundPort = 0;
}
