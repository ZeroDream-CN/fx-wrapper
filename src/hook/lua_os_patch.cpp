#include "lua_os_patch.h"

#include "citizen_lua_api.h"
#include "binary_module.h"
#include "fx_log.h"
#include "hook_util.h"
#include "platform/platform.h"

#if !defined(_WIN32)
#include "platform/linux_fxserver_launch.h"
#include "platform/linux_thread.h"
#endif

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

constexpr char kExecuteSymbolName[] = "LuaOSExecute";
constexpr char kRemoveSymbolName[] = "LuaOSRemove";
constexpr char kRenameSymbolName[] = "LuaOSRename";

using LuaCFunction = int (*)(lua_State*);

LuaCFunction g_originalLuaOsExecute = nullptr;
LuaCFunction g_originalLuaOsRemove = nullptr;
LuaCFunction g_originalLuaOsRename = nullptr;
CitizenLuaApi g_luaApi{};
std::atomic<bool> g_luaOsHookInstalled{false};
std::atomic<bool> g_luaOsHookScheduled{false};
std::atomic<bool> g_luaExecuteHookInstalled{false};
std::atomic<bool> g_luaRemoveHookInstalled{false};
std::atomic<bool> g_luaRenameHookInstalled{false};
std::atomic<bool> g_loggedLocateFailure{false};
std::atomic<bool> g_loggedResolveFailure{false};

constexpr int kLuaHookInstallAttempts = 600;
constexpr std::uint32_t kLuaHookInstallInitialDelayMs = 500;
constexpr std::uint32_t kLuaHookInstallRetryDelayMs = 100;

bool UsesVfsPath(const char* path) {
    return path != nullptr && path[0] == '@';
}

int RealLuaOsExecute(lua_State* L) {
    if (g_luaApi.lua_pushstring == nullptr) {
        DebugLogLine("[fx-hook] RealLuaOsExecute called without resolved Lua API");
        if (g_originalLuaOsExecute != nullptr) {
            return g_originalLuaOsExecute(L);
        }

        return 0;
    }

    const char* command = ReadLuaOptStringArg1(L);
    if (command == nullptr) {
        PushLuaBoolean(L, true);
        return 1;
    }

    DebugLog("[fx-hook] os.execute command: ");
    DebugLog(command);
    DebugLogLine("");

    errno = 0;
#if defined(_WIN32)
    const int status = std::system(command);
#else
    const int status = RunMuslShellCommand(command);
#endif

    char message[128];
    std::snprintf(message, sizeof(message), "[fx-hook] os.execute system() returned %d, errno=%d", status, errno);
    DebugLogLine(message);

    return PushLuaExecResult(g_luaApi, L, status);
}

int RealLuaOsRemove(lua_State* L) {
    if (g_luaApi.lua_pushstring == nullptr) {
        if (g_originalLuaOsRemove != nullptr) {
            return g_originalLuaOsRemove(L);
        }

        return 0;
    }

    const char* filename = ReadLuaStringArgFromTop(L, -1);
    if (filename == nullptr) {
        if (g_originalLuaOsRemove != nullptr) {
            return g_originalLuaOsRemove(L);
        }

        return 0;
    }

    if (UsesVfsPath(filename)) {
        if (g_originalLuaOsRemove != nullptr) {
            return g_originalLuaOsRemove(L);
        }

        return 0;
    }

    DebugLog("[fx-hook] os.remove path: ");
    DebugLog(filename);
    DebugLogLine("");

    errno = 0;
    const bool success = std::remove(filename) == 0;
    return PushLuaFileResult(g_luaApi, L, success, filename);
}

int RealLuaOsRename(lua_State* L) {
    if (g_luaApi.lua_pushstring == nullptr) {
        if (g_originalLuaOsRename != nullptr) {
            return g_originalLuaOsRename(L);
        }

        return 0;
    }

    const char* fromName = ReadLuaStringArgFromTop(L, -2);
    const char* toName = ReadLuaStringArgFromTop(L, -1);
    if (fromName == nullptr || toName == nullptr) {
        if (g_originalLuaOsRename != nullptr) {
            return g_originalLuaOsRename(L);
        }

        return 0;
    }

    if (UsesVfsPath(fromName) || UsesVfsPath(toName)) {
        if (g_originalLuaOsRename != nullptr) {
            return g_originalLuaOsRename(L);
        }

        return 0;
    }

    DebugLog("[fx-hook] os.rename path: ");
    DebugLog(fromName);
    DebugLog(" -> ");
    DebugLog(toName);
    DebugLogLine("");

    errno = 0;
    const bool success = std::rename(fromName, toName) == 0;
    return PushLuaRenameResult(g_luaApi, L, success, fromName, toName);
}

bool InstallLuaOsHooksOnce() {
    if (g_luaOsHookInstalled.load()) {
        return true;
    }

    ModuleHandle module = GetModuleHandleByName(ScriptingLuaModuleName());
    const bool luaModuleMapped = IsModuleMapped("citizen-scripting-lua");
    if (module == nullptr && !luaModuleMapped) {
        return false;
    }

    ModuleImage image{};
    if (!GetModuleImage(module, image) && !GetModuleImageByFragment("citizen-scripting-lua", image)) {
        return false;
    }

    void* sandboxExecute = FindSystemLibsFunctionFromOpenOs(image, "execute");
    if (sandboxExecute == nullptr) {
        sandboxExecute = FindSandboxExecuteFunction(image);
    }
    if (sandboxExecute == nullptr) {
        sandboxExecute = FindSystemLibsFunction(image, "execute");
    }

    void* sandboxRemove = FindSystemLibsFunctionFromOpenOs(image, "remove");
    void* sandboxRename = FindSystemLibsFunctionFromOpenOs(image, "rename");
    if (sandboxExecute != nullptr) {
        if (sandboxRemove == nullptr) {
            sandboxRemove = FindSiblingSandboxFunction(image, sandboxExecute, "execute", "remove");
        }
        if (sandboxRename == nullptr) {
            sandboxRename = FindSiblingSandboxFunction(image, sandboxExecute, "execute", "rename");
        }
    }

    if (sandboxRemove == nullptr) {
        sandboxRemove = FindSystemLibsFunction(image, "remove");
    }
    if (sandboxRemove == nullptr) {
        sandboxRemove = FindFunctionContainingStringMarker(image, "remove_and_check_if_empty");
    }

    if (sandboxRename == nullptr) {
        sandboxRename = FindSystemLibsFunction(image, "rename");
    }

    if (sandboxExecute == nullptr || sandboxRemove == nullptr || sandboxRename == nullptr) {
        if (!luaModuleMapped) {
            return false;
        }

        if (!g_loggedLocateFailure.exchange(true)) {
            DebugLogLine("[fx-hook] Failed to locate sandbox os functions in systemLibs");
        }

        return false;
    }

    if (!ResolveCitizenLuaApi(image, sandboxExecute, g_luaApi)) {
        if (!g_loggedResolveFailure.exchange(true)) {
            DebugLogLine("[fx-hook] Failed to resolve Lua API from sandbox os.execute");
        }

        return false;
    }

    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    if (!IsAddressExecutable(image, sandboxExecute) || !IsAddressExecutable(image, sandboxRemove) ||
        !IsAddressExecutable(image, sandboxRename)) {
        return false;
    }

    if (!g_luaExecuteHookInstalled.load()) {
        if (!CreateAndEnableHook(
                sandboxExecute,
                reinterpret_cast<void*>(&RealLuaOsExecute),
                reinterpret_cast<void**>(&g_originalLuaOsExecute),
                kExecuteSymbolName)) {
            return false;
        }

        g_luaExecuteHookInstalled.store(true);
    }

    if (!g_luaRemoveHookInstalled.load()) {
        if (!CreateAndEnableHook(
                sandboxRemove,
                reinterpret_cast<void*>(&RealLuaOsRemove),
                reinterpret_cast<void**>(&g_originalLuaOsRemove),
                kRemoveSymbolName)) {
            return false;
        }

        g_luaRemoveHookInstalled.store(true);
    }

    if (!g_luaRenameHookInstalled.load()) {
        if (!CreateAndEnableHook(
                sandboxRename,
                reinterpret_cast<void*>(&RealLuaOsRename),
                reinterpret_cast<void**>(&g_originalLuaOsRename),
                kRenameSymbolName)) {
            return false;
        }

        g_luaRenameHookInstalled.store(true);
    }

    g_luaOsHookInstalled.store(true);
    DebugLogLine("[fx-hook] LuaOSExecute hook installed");
    DebugLogLine("[fx-hook] LuaOSRemove hook installed");
    DebugLogLine("[fx-hook] LuaOSRename hook installed");
    NotifyHookStageInstalled(HookInstallStage::LuaOs);
    return true;
}

void LuaOsHookWorker() {
    PlatformSleepMs(kLuaHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kLuaHookInstallAttempts; ++attempt) {
        if (InstallLuaOsHooksOnce()) {
            return;
        }

        PlatformSleepMs(kLuaHookInstallRetryDelayMs);
    }

    DebugLogLine("[fx-hook] Timed out installing Lua OS hooks");
}

static void LuaOsHookWorkerEntry() {
    LuaOsHookWorker();
}

}  // namespace

void ScheduleLuaOsExecuteHookInstall() {
    bool expected = false;
    if (!g_luaOsHookScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

#if defined(_WIN32)
    std::thread(LuaOsHookWorker).detach();
#else
    LaunchDetachedThread(LuaOsHookWorkerEntry);
#endif
}
