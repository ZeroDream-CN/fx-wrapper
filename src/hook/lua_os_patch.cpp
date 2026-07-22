#include "lua_os_patch.h"

#include "citizen_lua_api.h"
#include "fx_log.h"
#include "hook_util.h"

#include <MinHook.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

constexpr char kScriptingLuaModule[] = "citizen-scripting-lua.dll";
constexpr char kExecuteSymbolName[] = "LuaOSExecute";
constexpr char kRemoveSymbolName[] = "LuaOSRemove";
constexpr char kRenameSymbolName[] = "LuaOSRename";

using LuaCFunction = int(__cdecl*)(lua_State*);

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
constexpr DWORD kLuaHookInstallInitialDelayMs = 500;
constexpr DWORD kLuaHookInstallRetryDelayMs = 100;

bool UsesVfsPath(const char* path) {
    return path != nullptr && path[0] == '@';
}

int __cdecl RealLuaOsExecute(lua_State* L) {
    if (g_luaApi.lua_pushstring == nullptr) {
        OutputDebugStringW(L"[fx-hook] RealLuaOsExecute called without resolved Lua API\n");
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

    OutputDebugStringA("[fx-hook] os.execute command: ");
    OutputDebugStringA(command);
    OutputDebugStringA("\n");

    errno = 0;
    const int status = std::system(command);

    char message[128];
    std::snprintf(message, sizeof(message), "[fx-hook] os.execute system() returned %d, errno=%d\n", status, errno);
    OutputDebugStringA(message);

    return PushLuaExecResult(g_luaApi, L, status);
}

int __cdecl RealLuaOsRemove(lua_State* L) {
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

    OutputDebugStringA("[fx-hook] os.remove path: ");
    OutputDebugStringA(filename);
    OutputDebugStringA("\n");

    errno = 0;
    const bool success = std::remove(filename) == 0;
    return PushLuaFileResult(g_luaApi, L, success, filename);
}

int __cdecl RealLuaOsRename(lua_State* L) {
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

    OutputDebugStringA("[fx-hook] os.rename path: ");
    OutputDebugStringA(fromName);
    OutputDebugStringA(" -> ");
    OutputDebugStringA(toName);
    OutputDebugStringA("\n");

    errno = 0;
    const bool success = std::rename(fromName, toName) == 0;
    return PushLuaRenameResult(g_luaApi, L, success, fromName, toName);
}

bool InstallLuaOsHooksOnce() {
    if (g_luaOsHookInstalled.load()) {
        return true;
    }

    HMODULE module = GetModuleHandleA(kScriptingLuaModule);
    if (module == nullptr) {
        return false;
    }

    void* sandboxExecute = FindSystemLibsFunction(module, "execute");
    void* sandboxRemove = FindSystemLibsFunction(module, "remove");
    void* sandboxRename = FindSystemLibsFunction(module, "rename");
    if (sandboxExecute == nullptr || sandboxRemove == nullptr || sandboxRename == nullptr) {
        if (!g_loggedLocateFailure.exchange(true)) {
            OutputDebugStringW(L"[fx-hook] Failed to locate sandbox os functions in systemLibs\n");
        }

        return false;
    }

    if (!ResolveCitizenLuaApi(module, sandboxExecute, g_luaApi)) {
        if (!g_loggedResolveFailure.exchange(true)) {
            OutputDebugStringW(L"[fx-hook] Failed to resolve Lua API from sandbox os.execute\n");
        }

        return false;
    }

    if (!EnsureMinHookInitialized()) {
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
    OutputDebugStringW(L"[fx-hook] LuaOSExecute hook installed\n");
    OutputDebugStringW(L"[fx-hook] LuaOSRemove hook installed\n");
    OutputDebugStringW(L"[fx-hook] LuaOSRename hook installed\n");
    NotifyHookStageInstalled(HookInstallStage::LuaOs);
    return true;
}

void LuaOsHookWorker() {
    Sleep(kLuaHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kLuaHookInstallAttempts; ++attempt) {
        if (InstallLuaOsHooksOnce()) {
            return;
        }

        Sleep(kLuaHookInstallRetryDelayMs);
    }

    OutputDebugStringW(L"[fx-hook] Timed out installing Lua OS hooks\n");
}

}  // namespace

void ScheduleLuaOsExecuteHookInstall() {
    bool expected = false;
    if (!g_luaOsHookScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread(LuaOsHookWorker).detach();
}
