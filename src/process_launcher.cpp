#include "process_launcher.h"

#include <filesystem>

namespace {

std::wstring QuoteArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    for (size_t i = 0; i < argument.size(); ++i) {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            ++backslashes;
            ++i;
        }

        if (i == argument.size()) {
            quoted.append(backslashes * 2, L'\\');
            break;
        }

        if (argument[i] == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(argument[i]);
        }
    }

    quoted.push_back(L'"');
    return quoted;
}

bool FileExists(const std::wstring& path) {
    return std::filesystem::exists(path);
}

}  // namespace

std::wstring GetExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return L"";
        }

        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path(path).parent_path().wstring();
        }

        path.resize(path.size() * 2);
    }
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& fileName) {
    return (std::filesystem::path(directory) / fileName).wstring();
}

std::wstring BuildCommandLine(int argc, wchar_t* argv[]) {
    std::wstring commandLine;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteArgument(argv[i]);
    }
    return commandLine;
}

bool LaunchFXServerSuspended(int argc, wchar_t* argv[], LaunchedProcess& outProcess, std::wstring& outError) {
    const std::wstring wrapperDir = GetExecutableDirectory();
    if (wrapperDir.empty()) {
        outError = L"Failed to resolve wrapper executable directory.";
        return false;
    }

    const std::wstring fxServerPath = JoinPath(wrapperDir, L"FXServer.exe");
    if (!FileExists(fxServerPath)) {
        outError = L"FXServer.exe was not found next to FXWrapper.exe:\n" + fxServerPath;
        return false;
    }

    outProcess.commandLine = BuildCommandLine(argc, argv);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    std::wstring mutableCommandLine = outProcess.commandLine;
    if (!CreateProcessW(
            fxServerPath.c_str(),
            mutableCommandLine.empty() ? nullptr : mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED,
            nullptr,
            wrapperDir.c_str(),
            &startupInfo,
            &outProcess.processInfo)) {
        outError = L"CreateProcessW failed for FXServer.exe. GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    return true;
}

void TerminateLaunchedProcess(LaunchedProcess& process) {
    if (process.processInfo.hProcess != nullptr) {
        TerminateProcess(process.processInfo.hProcess, 1);
    }

    if (process.processInfo.hThread != nullptr) {
        CloseHandle(process.processInfo.hThread);
        process.processInfo.hThread = nullptr;
    }

    if (process.processInfo.hProcess != nullptr) {
        CloseHandle(process.processInfo.hProcess);
        process.processInfo.hProcess = nullptr;
    }
}

DWORD WaitForProcessExit(LaunchedProcess& process) {
    WaitForSingleObject(process.processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(process.processInfo.hProcess, &exitCode);

    CloseHandle(process.processInfo.hThread);
    CloseHandle(process.processInfo.hProcess);
    process.processInfo.hThread = nullptr;
    process.processInfo.hProcess = nullptr;

    return exitCode;
}

bool ResumeLaunchedProcess(LaunchedProcess& process, std::wstring& outError) {
    if (ResumeThread(process.processInfo.hThread) == static_cast<DWORD>(-1)) {
        outError = L"ResumeThread failed. GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    return true;
}
