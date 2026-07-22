#include "hooks.h"

#include "fx_log.h"
#include "hook_util.h"

#include <atomic>
#include <string>
#include <thread>

namespace {

constexpr char kScriptingCoreModule[] = "citizen-scripting-core.dll";

constexpr char kChildProcessAllowSpawnSymbol[] =
    "?ScriptingChildProcessAllowSpawn@fx@@YA_NPEAVResource@1@@Z";
constexpr char kFilesystemAllowWriteSymbol[] =
    "?ScriptingFilesystemAllowWrite@fx@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEAVResource@1@@Z";
constexpr char kWorkerAllowSpawnSymbol[] =
    "?ScriptingWorkerAllowSpawn@fx@@YA_NPEAVResource@1@@Z";

using ChildProcessAllowSpawnFn = bool(__cdecl*)(void*);
using FilesystemAllowWriteFn = bool(__cdecl*)(const std::string&, void*);
using WorkerAllowSpawnFn = bool(__cdecl*)(void*);

ChildProcessAllowSpawnFn g_originalChildProcessAllowSpawn = nullptr;
FilesystemAllowWriteFn g_originalFilesystemAllowWrite = nullptr;
WorkerAllowSpawnFn g_originalWorkerAllowSpawn = nullptr;

std::atomic<bool> g_hooksInstalled{false};
std::atomic<bool> g_hookInstallScheduled{false};
std::atomic<bool> g_childHookInstalled{false};
std::atomic<bool> g_filesystemHookInstalled{false};
std::atomic<bool> g_workerHookInstalled{false};

constexpr int kHookInstallAttempts = 600;
constexpr DWORD kHookInstallInitialDelayMs = 500;
constexpr DWORD kHookInstallRetryDelayMs = 100;

bool __cdecl Hook_ChildProcessAllowSpawn(void* /*resource*/) {
    return true;
}

bool __cdecl Hook_FilesystemAllowWrite(const std::string& /*path*/, void* /*resource*/) {
    return true;
}

bool __cdecl Hook_WorkerAllowSpawn(void* /*resource*/) {
    return true;
}

bool InstallScriptingHooksOnce() {
    if (g_hooksInstalled.load()) {
        return true;
    }

    HMODULE module = GetModuleHandleA(kScriptingCoreModule);
    if (module == nullptr) {
        return false;
    }

    if (!EnsureMinHookInitialized()) {
        return false;
    }

    void* childProcessTarget = reinterpret_cast<void*>(GetProcAddress(module, kChildProcessAllowSpawnSymbol));
    void* filesystemTarget = reinterpret_cast<void*>(GetProcAddress(module, kFilesystemAllowWriteSymbol));
    void* workerTarget = reinterpret_cast<void*>(GetProcAddress(module, kWorkerAllowSpawnSymbol));

    if (childProcessTarget == nullptr || filesystemTarget == nullptr || workerTarget == nullptr) {
        return false;
    }

    if (!EnsureMinHookInitialized()) {
        return false;
    }

    if (!g_childHookInstalled.load()) {
        if (!CreateAndEnableHook(
                childProcessTarget,
                reinterpret_cast<void*>(&Hook_ChildProcessAllowSpawn),
                reinterpret_cast<void**>(&g_originalChildProcessAllowSpawn),
                kChildProcessAllowSpawnSymbol)) {
            return false;
        }

        g_childHookInstalled.store(true);
    }

    if (!g_filesystemHookInstalled.load()) {
        if (!CreateAndEnableHook(
                filesystemTarget,
                reinterpret_cast<void*>(&Hook_FilesystemAllowWrite),
                reinterpret_cast<void**>(&g_originalFilesystemAllowWrite),
                kFilesystemAllowWriteSymbol)) {
            return false;
        }

        g_filesystemHookInstalled.store(true);
    }

    if (!g_workerHookInstalled.load()) {
        if (!CreateAndEnableHook(
                workerTarget,
                reinterpret_cast<void*>(&Hook_WorkerAllowSpawn),
                reinterpret_cast<void**>(&g_originalWorkerAllowSpawn),
                kWorkerAllowSpawnSymbol)) {
            return false;
        }

        g_workerHookInstalled.store(true);
    }

    if (g_childHookInstalled.load() && g_filesystemHookInstalled.load() && g_workerHookInstalled.load()) {
        g_hooksInstalled.store(true);
        OutputDebugStringW(L"[fx-hook] All scripting hooks installed\n");
        NotifyHookStageInstalled(HookInstallStage::ScriptingCore);
        return true;
    }

    OutputDebugStringW(L"[fx-hook] Failed to install one or more scripting hooks\n");
    return false;
}

void HookInstallWorker() {
    Sleep(kHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kHookInstallAttempts; ++attempt) {
        if (InstallScriptingHooksOnce()) {
            return;
        }

        Sleep(kHookInstallRetryDelayMs);
    }

    OutputDebugStringW(L"[fx-hook] Timed out installing scripting hooks\n");
}

}  // namespace

void ScheduleScriptingHookInstall() {
    bool expected = false;
    if (!g_hookInstallScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread(HookInstallWorker).detach();
}

bool InstallScriptingHooks() {
    ScheduleScriptingHookInstall();
    return true;
}
