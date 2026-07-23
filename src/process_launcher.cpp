#include "process_launcher.h"

#include "platform/platform.h"

#if !defined(_WIN32)
#include "platform/linux_fxserver_launch.h"
#endif

#include <filesystem>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::string QuoteArgument(const std::string& argument) {
    if (argument.empty()) {
        return "\"\"";
    }

    if (argument.find_first_of(" \t\"") == std::string::npos) {
        return argument;
    }

    std::string quoted = "\"";
    for (size_t i = 0; i < argument.size(); ++i) {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == '\\') {
            ++backslashes;
            ++i;
        }

        if (i == argument.size()) {
            quoted.append(backslashes * 2, '\\');
            break;
        }

        if (argument[i] == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(argument[i]);
        }
    }

    quoted.push_back('"');
    return quoted;
}

#if !defined(_WIN32)

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>

bool SetEnvValue(const std::string& key, const std::string& value) {
    return setenv(key.c_str(), value.c_str(), 1) == 0;
}

#endif

}  // namespace

std::string BuildCommandLine(int argc, char* argv[]) {
    std::ostringstream commandLine;
    for (int index = 1; index < argc; ++index) {
        if (index > 1) {
            commandLine << ' ';
        }
        commandLine << QuoteArgument(argv[index]);
    }
    return commandLine.str();
}

std::string GetHookLibraryPath() {
    return JoinPath(GetExecutableDirectory(), HookLibraryFileName());
}

#if defined(_WIN32)

bool PrepareHookedEnvironment(std::string& /*outError*/) {
    return true;
}

bool LaunchFXServer(int argc, char* argv[], LaunchedProcess& outProcess, std::string& outError) {
    const std::string wrapperDir = GetExecutableDirectory();
    if (wrapperDir.empty()) {
        outError = "Failed to resolve wrapper executable directory.";
        return false;
    }

    const std::string fxServerPath = JoinPath(wrapperDir, FxServerExecutableName());
    if (!std::filesystem::exists(fxServerPath)) {
        outError = "FXServer.exe was not found next to FXWrapper:\n" + fxServerPath;
        return false;
    }

    outProcess.commandLine = BuildCommandLine(argc, argv);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::vector<char> mutableCommandLine(outProcess.commandLine.begin(), outProcess.commandLine.end());
    mutableCommandLine.push_back('\0');

    if (!CreateProcessA(
            fxServerPath.c_str(),
            mutableCommandLine.empty() ? nullptr : mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED,
            nullptr,
            wrapperDir.c_str(),
            &startupInfo,
            &processInfo)) {
        outError = "CreateProcessA failed for FXServer.exe. GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    outProcess.processHandle = processInfo.hProcess;
    outProcess.threadHandle = processInfo.hThread;
    return true;
}

void TerminateLaunchedProcess(LaunchedProcess& process) {
    if (process.processHandle != nullptr) {
        TerminateProcess(static_cast<HANDLE>(process.processHandle), 1);
    }

    if (process.threadHandle != nullptr) {
        CloseHandle(static_cast<HANDLE>(process.threadHandle));
        process.threadHandle = nullptr;
    }

    if (process.processHandle != nullptr) {
        CloseHandle(static_cast<HANDLE>(process.processHandle));
        process.processHandle = nullptr;
    }
}

int WaitForProcessExit(LaunchedProcess& process) {
    WaitForSingleObject(static_cast<HANDLE>(process.processHandle), INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(static_cast<HANDLE>(process.processHandle), &exitCode);

    CloseHandle(static_cast<HANDLE>(process.threadHandle));
    CloseHandle(static_cast<HANDLE>(process.processHandle));
    process.threadHandle = nullptr;
    process.processHandle = nullptr;

    return static_cast<int>(exitCode);
}

bool ResumeLaunchedProcess(LaunchedProcess& process, std::string& outError) {
    if (ResumeThread(static_cast<HANDLE>(process.threadHandle)) == static_cast<DWORD>(-1)) {
        outError = "ResumeThread failed. GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    return true;
}

#else

bool PrepareHookedEnvironment(std::string& outError) {
    const std::string hookLibraryPath = GetHookLibraryPath();
    if (!PathExistsOnDisk(hookLibraryPath)) {
        outError = "libfx-hook.so was not found next to FXWrapper:\n" + hookLibraryPath;
        return false;
    }

    return true;
}

bool LaunchFXServer(int argc, char* argv[], LaunchedProcess& outProcess, std::string& outError) {
    const std::string wrapperDir = GetExecutableDirectory();
    if (wrapperDir.empty()) {
        outError = "Failed to resolve wrapper executable directory.";
        return false;
    }

    const std::string fxServerPath = JoinPath(wrapperDir, FxServerExecutableName());
    if (!PathExistsOnDisk(fxServerPath)) {
        outError = "FXServer was not found next to FXWrapper:\n" + fxServerPath;
        return false;
    }

    if (!PrepareHookedEnvironment(outError)) {
        return false;
    }

    outProcess.commandLine = BuildCommandLine(argc, argv);

    std::vector<std::string> argStorage;
    std::vector<char*> argPointers;
    argStorage.emplace_back(fxServerPath);
    argPointers.push_back(argStorage.back().data());

    for (int index = 1; index < argc; ++index) {
        argStorage.emplace_back(argv[index]);
        argPointers.push_back(argStorage.back().data());
    }
    argPointers.push_back(nullptr);

    const MuslLaunchPlan plan = BuildMuslLaunchPlan(fxServerPath.c_str(), argPointers.data());
    if (plan.execPath.empty() || plan.argv.empty()) {
        outError = "Failed to build FXServer launch plan.";
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        outError = "fork() failed.";
        return false;
    }

    if (pid == 0) {
        const std::string hookLibraryPath = GetHookLibraryPath();
        const std::string fxServerDir = GetDirectoryName(fxServerPath);
        const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
        const std::string alpineLibraryPath =
            alpineRoot.empty() ? std::string{} : BuildAlpineLibraryPath(alpineRoot);
        extern char** environ;
        char* const* injectedEnv =
            BuildFxServerEnviron(environ, hookLibraryPath.c_str(), alpineLibraryPath);
        execve(plan.execPath.c_str(), plan.argv.data(), injectedEnv);
        _exit(127);
    }

    outProcess.pid = pid;
    return true;
}

void TerminateLaunchedProcess(LaunchedProcess& process) {
    if (process.pid > 0) {
        kill(process.pid, SIGKILL);
        waitpid(process.pid, nullptr, 0);
        process.pid = -1;
    }
}

int WaitForProcessExit(LaunchedProcess& process) {
    int status = 1;
    if (process.pid > 0) {
        waitpid(process.pid, &status, 0);
        process.pid = -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return 1;
}

bool ResumeLaunchedProcess(LaunchedProcess& /*process*/, std::string& /*outError*/) {
    return true;
}

#endif
