#pragma once

#include <cstdint>
#include <string>

struct LaunchedProcess {
#if defined(_WIN32)
    void* processHandle = nullptr;
    void* threadHandle = nullptr;
#else
    int pid = -1;
#endif
    std::string commandLine;
};

std::string BuildCommandLine(int argc, char* argv[]);
bool LaunchFXServer(int argc, char* argv[], LaunchedProcess& outProcess, std::string& outError);
void TerminateLaunchedProcess(LaunchedProcess& process);
int WaitForProcessExit(LaunchedProcess& process);
bool ResumeLaunchedProcess(LaunchedProcess& process, std::string& outError);

std::string GetHookLibraryPath();
bool PrepareHookedEnvironment(std::string& outError);
