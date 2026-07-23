#include "hooks.h"

#include "fx_log.h"
#include "hook_util.h"
#include "platform/platform.h"

#if !defined(_WIN32)
#include "platform/linux_thread.h"
#endif

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

#if defined(_WIN32)
constexpr const char* kChildProcessAllowSpawnSymbols[] = {
    "?ScriptingChildProcessAllowSpawn@fx@@YA_NPEAVResource@1@@Z",
    nullptr,
};
constexpr const char* kFilesystemAllowWriteSymbols[] = {
    "?ScriptingFilesystemAllowWrite@fx@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PEAVResource@1@@Z",
    nullptr,
};
constexpr const char* kWorkerAllowSpawnSymbols[] = {
    "?ScriptingWorkerAllowSpawn@fx@@YA_NPEAVResource@1@@Z",
    nullptr,
};
#else
constexpr const char* kChildProcessAllowSpawnSymbols[] = {
    "_ZN2fx31ScriptingChildProcessAllowSpawnEPNS_8ResourceE",
    "_ZN2fx28ScriptingChildProcessAllowSpawnEPNS_8ResourceE",
    nullptr,
};
constexpr const char* kFilesystemAllowWriteSymbols[] = {
    "_ZN2fx29ScriptingFilesystemAllowWriteERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEPNS_8ResourceE",
    "_ZN2fx27ScriptingFilesystemAllowWriteERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEEPNS_8ResourceE",
    "_ZN2fx27ScriptingFilesystemAllowWriteERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEPNS_8ResourceE",
    nullptr,
};
constexpr const char* kWorkerAllowSpawnSymbols[] = {
    "_ZN2fx25ScriptingWorkerAllowSpawnEPNS_8ResourceE",
    "_ZN2fx22ScriptingWorkerAllowSpawnEPNS_8ResourceE",
    nullptr,
};
#endif

using ChildProcessAllowSpawnFn = bool (*)(void*);
using FilesystemAllowWriteFn = bool (*)(const std::string&, void*);
using WorkerAllowSpawnFn = bool (*)(void*);

ChildProcessAllowSpawnFn g_originalChildProcessAllowSpawn = nullptr;
FilesystemAllowWriteFn g_originalFilesystemAllowWrite = nullptr;
WorkerAllowSpawnFn g_originalWorkerAllowSpawn = nullptr;

std::atomic<bool> g_hooksInstalled{false};
std::atomic<bool> g_hookInstallScheduled{false};
std::atomic<bool> g_childHookInstalled{false};
std::atomic<bool> g_filesystemHookInstalled{false};
std::atomic<bool> g_workerHookInstalled{false};

constexpr int kHookInstallAttempts = 600;
constexpr std::uint32_t kHookInstallInitialDelayMs = 500;
constexpr std::uint32_t kHookInstallRetryDelayMs = 100;

bool HookChildProcessAllowSpawn(void* /*resource*/) {
    return true;
}

bool HookFilesystemAllowWrite(const std::string& /*path*/, void* /*resource*/) {
    return true;
}

bool HookWorkerAllowSpawn(void* /*resource*/) {
    return true;
}

void* ResolveSymbol(ModuleHandle module, const char* const* symbolNames) {
    std::vector<const char*> candidates;
    for (const char* const* cursor = symbolNames; *cursor != nullptr; ++cursor) {
        candidates.push_back(*cursor);
    }

    return GetModuleSymbol(module, candidates);
}

bool InstallScriptingHooksOnce() {
    if (g_hooksInstalled.load()) {
        return true;
    }

    ModuleHandle module = GetModuleHandleByName(ScriptingCoreModuleName());
    if (module == nullptr && !IsModuleMapped("citizen-scripting-core")) {
        return false;
    }

    DebugLogLine("[fx-hook] citizen-scripting-core detected, resolving sandbox symbols");

    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    void* childProcessTarget = ResolveSymbol(module, kChildProcessAllowSpawnSymbols);
    void* filesystemTarget = ResolveSymbol(module, kFilesystemAllowWriteSymbols);
    void* workerTarget = ResolveSymbol(module, kWorkerAllowSpawnSymbols);

    if (childProcessTarget == nullptr || filesystemTarget == nullptr || workerTarget == nullptr) {
        if (childProcessTarget == nullptr) {
            DebugLogLine("[fx-hook] Failed to resolve ScriptingChildProcessAllowSpawn symbol");
        }
        if (filesystemTarget == nullptr) {
            DebugLogLine("[fx-hook] Failed to resolve ScriptingFilesystemAllowWrite symbol");
        }
        if (workerTarget == nullptr) {
            DebugLogLine("[fx-hook] Failed to resolve ScriptingWorkerAllowSpawn symbol");
        }
        return false;
    }

    if (!g_childHookInstalled.load()) {
        if (!CreateAndEnableHook(
                childProcessTarget,
                reinterpret_cast<void*>(&HookChildProcessAllowSpawn),
                reinterpret_cast<void**>(&g_originalChildProcessAllowSpawn),
                kChildProcessAllowSpawnSymbols[0])) {
            return false;
        }

        g_childHookInstalled.store(true);
    }

    if (!g_filesystemHookInstalled.load()) {
        if (!CreateAndEnableHook(
                filesystemTarget,
                reinterpret_cast<void*>(&HookFilesystemAllowWrite),
                reinterpret_cast<void**>(&g_originalFilesystemAllowWrite),
                kFilesystemAllowWriteSymbols[0])) {
            return false;
        }

        g_filesystemHookInstalled.store(true);
    }

    if (!g_workerHookInstalled.load()) {
        if (!CreateAndEnableHook(
                workerTarget,
                reinterpret_cast<void*>(&HookWorkerAllowSpawn),
                reinterpret_cast<void**>(&g_originalWorkerAllowSpawn),
                kWorkerAllowSpawnSymbols[0])) {
            return false;
        }

        g_workerHookInstalled.store(true);
    }

    if (g_childHookInstalled.load() && g_filesystemHookInstalled.load() && g_workerHookInstalled.load()) {
        g_hooksInstalled.store(true);
        DebugLogLine("[fx-hook] All scripting hooks installed");
        NotifyHookStageInstalled(HookInstallStage::ScriptingCore);
        return true;
    }

    DebugLogLine("[fx-hook] Failed to install one or more scripting hooks");
    return false;
}

void HookInstallWorker() {
    PlatformSleepMs(kHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kHookInstallAttempts; ++attempt) {
        if (InstallScriptingHooksOnce()) {
            return;
        }

        PlatformSleepMs(kHookInstallRetryDelayMs);
    }

    DebugLogLine("[fx-hook] Timed out installing scripting hooks");
}

static void HookInstallWorkerEntry() {
    HookInstallWorker();
}

}  // namespace

void ScheduleScriptingHookInstall() {
    bool expected = false;
    if (!g_hookInstallScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

#if defined(_WIN32)
    std::thread(HookInstallWorker).detach();
#else
    LaunchDetachedThread(HookInstallWorkerEntry);
#endif
}

bool InstallScriptingHooks() {
    ScheduleScriptingHookInstall();
    return true;
}
