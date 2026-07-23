#pragma once

#include <string>
#include <vector>

struct MuslLaunchPlan {
    std::string execPath;
    std::vector<std::string> argStorage;
    std::vector<char*> argv;
};

bool IsFxServerBinaryPath(const char* path);
bool IsMuslLoaderPath(const char* path);
// Resolve FXServer binary path from exec/spawn path and argv (supports musl-loader launches).
std::string ResolveFxServerPathForLaunch(const char* path, char* const argv[]);
bool PathExistsOnDisk(const std::string& path);
std::string GetDirectoryName(const std::string& path);
std::string DetectAlpineRootFromServerDir(const std::string& cfxServerDir);
std::string BuildAlpineLibraryPath(const std::string& alpineRoot);
std::string MergeColonSeparatedPaths(const std::string& preferredPrefix, const std::string& existingValue);
MuslLaunchPlan BuildMuslLaunchPlan(const char* fxServerPath, char* const argv[]);
// Build exec/spawn argv for FXServer, preserving an existing musl-loader command line.
MuslLaunchPlan BuildLaunchPlanForFxServerSpawn(
    const char* pathname,
    char* const argv[],
    const std::string& fxServerPath);
char* const* BuildFxServerEnviron(
    char* const envp[],
    const char* hookLibraryPath,
    const std::string& alpineLibraryPath);

// Run a shell command through Alpine musl + busybox (Linux FXServer only).
int RunMuslShellCommand(const std::string& command);

// Remove fx-hook preload entries before launching host (glibc) binaries.
char* const* SanitizeEnvironmentForHostBinary(char* const envp[]);

// Strip bootstrap LD_PRELOAD / Alpine LD_LIBRARY_PATH from the current process so
// forked children (e.g. txAdmin Node getconf) do not inherit hook injection.
void DropFxHookEnvironmentFromProcess();
