#include "hook_util.h"

#include "fx_log.h"
#include "platform/linux_fxserver_launch.h"

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <spawn.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>

extern char** environ;

namespace {

char g_hookLibraryPath[PATH_MAX]{};
std::atomic<bool> g_spawnHooksInstalled{false};

using ExecveFn = int (*)(const char* pathname, char* const argv[], char* const envp[]);
using ExecvpFn = int (*)(const char* file, char* const argv[]);
using ExecvpeFn = int (*)(const char* file, char* const argv[], char* const envp[]);
using PosixSpawnFn = int (*)(
    pid_t* pid,
    const char* path,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]);

ExecveFn g_realExecve = nullptr;
ExecvpFn g_realExecvp = nullptr;
ExecvpeFn g_realExecvpe = nullptr;
PosixSpawnFn g_realPosixSpawn = nullptr;
PosixSpawnFn g_realPosixSpawnp = nullptr;

void DropFxHookEnvironmentInChildAfterFork() {
    DropFxHookEnvironmentFromProcess();
}

void CacheHookLibraryPath() {
    if (g_hookLibraryPath[0] != '\0') {
        return;
    }

    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&CacheHookLibraryPath), &info) == 0 || info.dli_fname == nullptr) {
        return;
    }

    std::strncpy(g_hookLibraryPath, info.dli_fname, sizeof(g_hookLibraryPath) - 1);
}

ExecveFn ResolveRealExecve() {
    if (g_realExecve != nullptr) {
        return g_realExecve;
    }

    g_realExecve = reinterpret_cast<ExecveFn>(dlsym(RTLD_NEXT, "execve"));
    return g_realExecve;
}

ExecvpFn ResolveRealExecvp() {
    if (g_realExecvp != nullptr) {
        return g_realExecvp;
    }

    g_realExecvp = reinterpret_cast<ExecvpFn>(dlsym(RTLD_NEXT, "execvp"));
    return g_realExecvp;
}

ExecvpeFn ResolveRealExecvpe() {
    if (g_realExecvpe != nullptr) {
        return g_realExecvpe;
    }

    g_realExecvpe = reinterpret_cast<ExecvpeFn>(dlsym(RTLD_NEXT, "execvpe"));
    return g_realExecvpe;
}

void* ResolveLibcSymbol(const char* symbolName) {
    void* symbol = dlsym(RTLD_NEXT, symbolName);
    if (symbol == nullptr) {
        symbol = dlsym(RTLD_DEFAULT, symbolName);
    }
    return symbol;
}

bool IsFxServerLaunchRequest(const char* path, char* const argv[]) {
    if (!ResolveFxServerPathForLaunch(path, argv).empty()) {
        return true;
    }

    if (!IsMuslLoaderPath(path) || argv == nullptr) {
        return false;
    }

    for (char* const* cursor = argv; *cursor != nullptr; ++cursor) {
        if (IsFxServerBinaryPath(*cursor)) {
            return true;
        }
    }

    return false;
}

int LaunchFxServerWithHooks(const char* pathname, char* const argv[], char* const envp[]) {
    ExecveFn realExecve = ResolveRealExecve();
    if (realExecve == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    const std::string fxServerPath = ResolveFxServerPathForLaunch(pathname, argv);
    if (fxServerPath.empty()) {
        errno = EINVAL;
        return -1;
    }

    CacheHookLibraryPath();
    const MuslLaunchPlan plan = BuildLaunchPlanForFxServerSpawn(pathname, argv, fxServerPath);
    if (plan.execPath.empty() || plan.argv.empty()) {
        errno = EINVAL;
        return -1;
    }

    const std::string fxServerDir = GetDirectoryName(fxServerPath);
    const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
    const std::string alpineLibraryPath = alpineRoot.empty() ? std::string{} : BuildAlpineLibraryPath(alpineRoot);
    char* const* injectedEnv =
        BuildFxServerEnviron(envp, g_hookLibraryPath, alpineLibraryPath);

    DebugLogLine("[fx-hook] Propagating hook library to child FXServer");
    return realExecve(plan.execPath.c_str(), plan.argv.data(), injectedEnv);
}

int SpawnFxServerWithHooks(
    pid_t* pid,
    const char* pathname,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[],
    PosixSpawnFn realPosixSpawn) {
    if (realPosixSpawn == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    const std::string fxServerPath = ResolveFxServerPathForLaunch(pathname, argv);
    if (fxServerPath.empty()) {
        errno = EINVAL;
        return -1;
    }

    CacheHookLibraryPath();
    const MuslLaunchPlan plan = BuildLaunchPlanForFxServerSpawn(pathname, argv, fxServerPath);
    if (plan.execPath.empty() || plan.argv.empty()) {
        errno = EINVAL;
        return -1;
    }

    const std::string fxServerDir = GetDirectoryName(fxServerPath);
    const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
    const std::string alpineLibraryPath = alpineRoot.empty() ? std::string{} : BuildAlpineLibraryPath(alpineRoot);
    char* const* injectedEnv =
        BuildFxServerEnviron(envp, g_hookLibraryPath, alpineLibraryPath);

    DebugLogLine("[fx-hook] Propagating hook library to child FXServer (posix_spawn)");
    return realPosixSpawn(pid, plan.execPath.c_str(), fileActions, attrp, plan.argv.data(), injectedEnv);
}

int ExecvpDetour(const char* file, char* const argv[]) {
    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, environ);
    }

    return ResolveRealExecvp()(file, argv);
}

int ExecvpeDetour(const char* file, char* const argv[], char* const envp[]) {
    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, envp);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return ResolveRealExecvpe()(file, argv, sanitizedEnv);
}

int PosixSpawnDetour(
    pid_t* pid,
    const char* path,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    if (IsFxServerLaunchRequest(path, argv)) {
        return SpawnFxServerWithHooks(pid, path, fileActions, attrp, argv, envp, g_realPosixSpawn);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return g_realPosixSpawn(pid, path, fileActions, attrp, argv, sanitizedEnv);
}

int PosixSpawnpDetour(
    pid_t* pid,
    const char* file,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    if (IsFxServerLaunchRequest(file, argv)) {
        return SpawnFxServerWithHooks(pid, file, fileActions, attrp, argv, envp, g_realPosixSpawnp);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return g_realPosixSpawnp(pid, file, fileActions, attrp, argv, sanitizedEnv);
}

bool InstallLibcSpawnHook(const char* symbolName, void* detour, void** original) {
    void* target = ResolveLibcSymbol(symbolName);
    if (target == nullptr) {
        DebugLog("[fx-hook] Failed to resolve libc symbol: ");
        DebugLogLine(symbolName);
        return false;
    }

    return CreateAndEnableHook(target, detour, original, symbolName);
}

}  // namespace

extern "C" int execve(const char* pathname, char* const argv[], char* const envp[]) {
    if (IsFxServerLaunchRequest(pathname, argv)) {
        return LaunchFxServerWithHooks(pathname, argv, envp);
    }

    ExecveFn realExecve = ResolveRealExecve();
    if (realExecve == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return realExecve(pathname, argv, sanitizedEnv);
}

extern "C" int execv(const char* pathname, char* const argv[]) {
    return execve(pathname, argv, environ);
}

extern "C" int execvp(const char* file, char* const argv[]) {
    if (g_realExecvp != nullptr) {
        return ExecvpDetour(file, argv);
    }

    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, environ);
    }

    ExecvpFn fallback = ResolveRealExecvp();
    if (fallback == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    return fallback(file, argv);
}

extern "C" int execvpe(const char* file, char* const argv[], char* const envp[]) {
    if (g_realExecvpe != nullptr) {
        return ExecvpeDetour(file, argv, envp);
    }

    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, envp);
    }

    ExecvpeFn fallback = ResolveRealExecvpe();
    if (fallback == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return fallback(file, argv, sanitizedEnv);
}

extern "C" int posix_spawn(
    pid_t* pid,
    const char* path,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    if (g_realPosixSpawn != nullptr) {
        return PosixSpawnDetour(pid, path, fileActions, attrp, argv, envp);
    }

    PosixSpawnFn fallback = reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawn"));
    if (fallback == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    if (IsFxServerLaunchRequest(path, argv)) {
        return SpawnFxServerWithHooks(pid, path, fileActions, attrp, argv, envp, fallback);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return fallback(pid, path, fileActions, attrp, argv, sanitizedEnv);
}

extern "C" int posix_spawnp(
    pid_t* pid,
    const char* file,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    if (g_realPosixSpawnp != nullptr) {
        return PosixSpawnpDetour(pid, file, fileActions, attrp, argv, envp);
    }

    PosixSpawnFn fallback = reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawnp"));
    if (fallback == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    if (IsFxServerLaunchRequest(file, argv)) {
        return SpawnFxServerWithHooks(pid, file, fileActions, attrp, argv, envp, fallback);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return fallback(pid, file, fileActions, attrp, argv, sanitizedEnv);
}

const char* GetHookLibraryPath() {
    CacheHookLibraryPath();
    return g_hookLibraryPath;
}

bool InstallProcessSpawnHooks() {
    if (g_spawnHooksInstalled.exchange(true)) {
        return true;
    }

    CacheHookLibraryPath();

    if (g_hookLibraryPath[0] == '\0') {
        DebugLogLine("[fx-hook] Failed to resolve hook library path");
        return false;
    }

    if (ResolveRealExecve() == nullptr) {
        DebugLogLine("[fx-hook] Failed to resolve real execve");
        return false;
    }

    EnsureHookEngineInitialized();

    InstallLibcSpawnHook(
        "execvp",
        reinterpret_cast<void*>(ExecvpDetour),
        reinterpret_cast<void**>(&g_realExecvp));
    InstallLibcSpawnHook(
        "execvpe",
        reinterpret_cast<void*>(ExecvpeDetour),
        reinterpret_cast<void**>(&g_realExecvpe));
    InstallLibcSpawnHook(
        "posix_spawn",
        reinterpret_cast<void*>(PosixSpawnDetour),
        reinterpret_cast<void**>(&g_realPosixSpawn));
    InstallLibcSpawnHook(
        "posix_spawnp",
        reinterpret_cast<void*>(PosixSpawnpDetour),
        reinterpret_cast<void**>(&g_realPosixSpawnp));

    pthread_atfork(nullptr, nullptr, DropFxHookEnvironmentInChildAfterFork);

    DebugLogLine("[fx-hook] Process spawn hook installed");
    NotifyHookStageInstalled(HookInstallStage::ProcessSpawn);
    return true;
}
