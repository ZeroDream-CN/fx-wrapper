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
    static ExecveFn realExecve = reinterpret_cast<ExecveFn>(dlsym(RTLD_NEXT, "execve"));
    return realExecve;
}

ExecvpFn ResolveRealExecvp() {
    static ExecvpFn realExecvp = reinterpret_cast<ExecvpFn>(dlsym(RTLD_NEXT, "execvp"));
    return realExecvp;
}

ExecvpeFn ResolveRealExecvpe() {
    static ExecvpeFn realExecvpe = reinterpret_cast<ExecvpeFn>(dlsym(RTLD_NEXT, "execvpe"));
    return realExecvpe;
}

PosixSpawnFn ResolveRealPosixSpawn() {
    static PosixSpawnFn realPosixSpawn =
        reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawn"));
    return realPosixSpawn;
}

PosixSpawnFn ResolveRealPosixSpawnp() {
    static PosixSpawnFn realPosixSpawnp =
        reinterpret_cast<PosixSpawnFn>(dlsym(RTLD_NEXT, "posix_spawnp"));
    return realPosixSpawnp;
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
    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, environ);
    }

    ExecvpFn realExecvp = ResolveRealExecvp();
    if (realExecvp == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    return realExecvp(file, argv);
}

extern "C" int execvpe(const char* file, char* const argv[], char* const envp[]) {
    if (IsFxServerLaunchRequest(file, argv)) {
        return LaunchFxServerWithHooks(file, argv, envp);
    }

    ExecvpeFn realExecvpe = ResolveRealExecvpe();
    if (realExecvpe == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return realExecvpe(file, argv, sanitizedEnv);
}

extern "C" int posix_spawn(
    pid_t* pid,
    const char* path,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    PosixSpawnFn realPosixSpawn = ResolveRealPosixSpawn();
    if (realPosixSpawn == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    if (IsFxServerLaunchRequest(path, argv)) {
        return SpawnFxServerWithHooks(pid, path, fileActions, attrp, argv, envp, realPosixSpawn);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return realPosixSpawn(pid, path, fileActions, attrp, argv, sanitizedEnv);
}

extern "C" int posix_spawnp(
    pid_t* pid,
    const char* file,
    const posix_spawn_file_actions_t* fileActions,
    const posix_spawnattr_t* attrp,
    char* const argv[],
    char* const envp[]) {
    PosixSpawnFn realPosixSpawnp = ResolveRealPosixSpawnp();
    if (realPosixSpawnp == nullptr) {
        errno = ENOSYS;
        return -1;
    }

    if (IsFxServerLaunchRequest(file, argv)) {
        return SpawnFxServerWithHooks(pid, file, fileActions, attrp, argv, envp, realPosixSpawnp);
    }

    char* const* sanitizedEnv = SanitizeEnvironmentForHostBinary(envp);
    return realPosixSpawnp(pid, file, fileActions, attrp, argv, sanitizedEnv);
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

    pthread_atfork(nullptr, nullptr, DropFxHookEnvironmentInChildAfterFork);

    DebugLogLine("[fx-hook] Process spawn hook installed");
    NotifyHookStageInstalled(HookInstallStage::ProcessSpawn);
    return true;
}
