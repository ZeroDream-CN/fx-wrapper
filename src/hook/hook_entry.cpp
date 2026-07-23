#include "hooks.h"
#include "hook_util.h"
#include "lua_os_patch.h"
#include "module_watcher.h"
#include "node_hooks.h"
#include "update_check.h"

#include "platform/platform.h"

#include <thread>

namespace {

void HookWorkerThread() {
    PlatformSleepMs(200);
    DebugLogLine("[fx-hook] Hook worker started");

    ScheduleLuaOsExecuteHookInstall();
    ScheduleNodePermissionHookInstall();
    StartModuleWatcher([]() { ScheduleScriptingHookInstall(); });
}

void UpdateCheckThread() {
    StartUpdateCheckAsync();
}

}  // namespace

#if defined(_WIN32)

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            InstallProcessSpawnHooks();
            std::thread(HookWorkerThread).detach();
            std::thread(UpdateCheckThread).detach();
            break;
        default:
            break;
    }

    return TRUE;
}

#else

#include "platform/linux_fxserver_launch.h"
#include "platform/linux_thread.h"

static void HookWorkerThreadEntry() {
    HookWorkerThread();
}

static void UpdateCheckThreadEntry() {
    UpdateCheckThread();
}

__attribute__((constructor)) static void FxHookLibraryInit() {
    InstallProcessSpawnHooks();
    DropFxHookEnvironmentFromProcess();
    LaunchDetachedThread(HookWorkerThreadEntry);
    LaunchDetachedThread(UpdateCheckThreadEntry);
}

#endif
