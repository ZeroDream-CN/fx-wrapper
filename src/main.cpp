#include "process_launcher.h"
#include "injector.h"

#include "platform/platform.h"

#include <filesystem>
#include <iostream>

namespace {

void PrintError(const std::string& message) {
    std::cerr << message << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    LaunchedProcess launchedProcess{};
    std::string error;

    if (!LaunchFXServer(argc, argv, launchedProcess, error)) {
        PrintError(error);
        return 1;
    }

#if defined(_WIN32)
    const std::string hookLibraryPath = GetHookLibraryPath();
    if (!std::filesystem::exists(hookLibraryPath)) {
        PrintError("fx-hook.dll was not found next to FXWrapper:\n" + hookLibraryPath);
        TerminateLaunchedProcess(launchedProcess);
        return 2;
    }

    if (!InjectHookLibrary(
            launchedProcess.processHandle,
            launchedProcess.threadHandle,
            hookLibraryPath,
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

    if (!WaitForInjectedModule(launchedProcess.processHandle, hookLibraryPath, 15000, error)) {
        PrintError(error);
        TerminateLaunchedProcess(launchedProcess);
        return 4;
    }
#endif

    return WaitForProcessExit(launchedProcess);
}
