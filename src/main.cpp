#include "process_launcher.h"
#include "injector.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void EnsureConsoleAttached() {
    if (GetConsoleWindow() != nullptr) {
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }

    AllocConsole();
}

void PrintError(const std::wstring& message) {
    std::wcerr << message << L'\n';
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    EnsureConsoleAttached();

    LaunchedProcess launchedProcess{};
    std::wstring error;

    if (!LaunchFXServerSuspended(argc, argv, launchedProcess, error)) {
        PrintError(error);
        return 1;
    }

    const std::wstring hookDllPath = JoinPath(GetExecutableDirectory(), L"fx-hook.dll");
    if (!std::filesystem::exists(hookDllPath)) {
        PrintError(L"fx-hook.dll was not found next to FXWrapper.exe:\n" + hookDllPath);
        TerminateLaunchedProcess(launchedProcess);
        return 2;
    }

    if (!InjectDll(
            launchedProcess.processInfo.hProcess,
            launchedProcess.processInfo.hThread,
            hookDllPath,
            error)) {
        PrintError(error);
        TerminateLaunchedProcess(launchedProcess);
        return 3;
    }

    if (!ResumeLaunchedProcess(launchedProcess, error)) {
        PrintError(error);
        TerminateLaunchedProcess(launchedProcess);
        return 3;
    }

    if (!WaitForInjectedModule(launchedProcess.processInfo.hProcess, hookDllPath, 5000, error)) {
        PrintError(error);
        TerminateLaunchedProcess(launchedProcess);
        return 4;
    }

    return static_cast<int>(WaitForProcessExit(launchedProcess));
}
