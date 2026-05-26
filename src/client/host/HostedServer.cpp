#include "HostedServer.hpp"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

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
} // namespace

bool HostedServer::start(HostConfigState const& config, std::string& outError)
{
    // TODO: Add Windows support (CreateProcess with redirected stdout, WaitForSingleObject for shutdown)\
    // TODO: Add persistence option (need to spawn process detached from parent, and a way to track it for shutdown if requested)
    // NOTE: Also need a way to differentiate between server spawned by client vs an independently launched server for
    // shutdown logic; maybe a command-line arg that makes the server write its bound port to a well-known file on
    // startup, which the client can read to find and connect to it?
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

    std::string addressArg = "--address=0.0.0.0";
    std::string portArg = "--port=" + std::to_string(config.port);

    int stdoutPipe[2];
    if (pipe(stdoutPipe) != 0) {
        outError = std::string("Failed to create pipe for server stdout: ") + std::strerror(errno);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    pid_t pid = fork();

    if (pid < 0) {
        outError = std::string("Failed to fork server process: ") + std::strerror(errno);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", outError.c_str());
        return false;
    }

    if (pid == 0) {
        // Setup child process to write its stdout to the pipe, then exec the server binary
        close(stdoutPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdoutPipe[1]);

        execl(serverPath.c_str(), serverPath.c_str(), addressArg.c_str(), portArg.c_str(), nullptr);

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

            // Look for a full line.
            size_t newline = output.find('\n');
            if (newline != std::string::npos) {
                std::string line = output.substr(0, newline);

                if (line.rfind("READY ", 0) == 0) {
                    int port = std::stoi(line.substr(6));
                    boundPort = static_cast<uint16_t>(port);
                    close(stdoutPipe[0]);
                    return true;
                }
            }
        }

        if (n == 0) {
            outError = "Server exited before reporting readiness";
            close(stdoutPipe[0]);
            return false;
        }

        if (n < 0) {
            outError = "Failed to read server stdout";
            close(stdoutPipe[0]);
            return false;
        }
    }

    SDL_Log("Hosted server process started with PID %d at port %d", pid, boundPort);

    return true;
}

void HostedServer::shutdown()
{
    SDL_Log("Shutting down hosted server...");
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
}

bool HostedServer::isRunning()
{
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
}

uint16_t HostedServer::port()
{
    return boundPort;
}
