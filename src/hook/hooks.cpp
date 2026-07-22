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

    const bool childOk = CreateAndEnableHook(
        childProcessTarget,
        reinterpret_cast<void*>(&Hook_ChildProcessAllowSpawn),
        reinterpret_cast<void**>(&g_originalChildProcessAllowSpawn),
        kChildProcessAllowSpawnSymbol);

    const bool filesystemOk = CreateAndEnableHook(
        filesystemTarget,
        reinterpret_cast<void*>(&Hook_FilesystemAllowWrite),
        reinterpret_cast<void**>(&g_originalFilesystemAllowWrite),
        kFilesystemAllowWriteSymbol);

    const bool workerOk = CreateAndEnableHook(
        workerTarget,
        reinterpret_cast<void*>(&Hook_WorkerAllowSpawn),
        reinterpret_cast<void**>(&g_originalWorkerAllowSpawn),
        kWorkerAllowSpawnSymbol);

    if (childOk && filesystemOk && workerOk) {
        g_hooksInstalled.store(true);
        OutputDebugStringW(L"[fx-hook] All scripting hooks installed\n");
        NotifyHookStageInstalled(HookInstallStage::ScriptingCore);
        return true;
    }

    OutputDebugStringW(L"[fx-hook] Failed to install one or more scripting hooks\n");
    return false;
}

void HookInstallWorker() {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (InstallScriptingHooksOnce()) {
            return;
        }

        Sleep(100);
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
