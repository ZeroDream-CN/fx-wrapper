#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct LaunchedProcess {
    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine;
};

// Build a command-line string from argv[1..], applying Windows quoting rules.
std::wstring BuildCommandLine(int argc, wchar_t* argv[]);

// Resolve {wrapper_dir}\FXServer.exe and launch suspended.
bool LaunchFXServerSuspended(int argc, wchar_t* argv[], LaunchedProcess& outProcess, std::wstring& outError);

// Terminate a launched process if injection or startup fails.
void TerminateLaunchedProcess(LaunchedProcess& process);

// Wait for the child process and return its exit code.
DWORD WaitForProcessExit(LaunchedProcess& process);

// Resume the main thread of a suspended process.
bool ResumeLaunchedProcess(LaunchedProcess& process, std::wstring& outError);

// Get the directory containing the current executable (no trailing slash).
std::wstring GetExecutableDirectory();

// Join two path segments with a backslash.
std::wstring JoinPath(const std::wstring& directory, const std::wstring& fileName);
