#include "hooks.h"
#include "hook_util.h"
#include "lua_os_patch.h"
#include "module_watcher.h"
#include "node_hooks.h"
#include "update_check.h"

#include <windows.h>

namespace {

DWORD WINAPI HookWorkerThread(LPVOID) {
    // Wait until DllMain returns and the loader lock is released.
    Sleep(200);

    InstallProcessSpawnHooks();
    ScheduleLuaOsExecuteHookInstall();
    ScheduleNodePermissionHookInstall();
    StartModuleWatcher([]() { ScheduleScriptingHookInstall(); });
    return 0;
}

DWORD WINAPI UpdateCheckThread(LPVOID) {
    StartUpdateCheckAsync();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            if (CreateThread(nullptr, 0, HookWorkerThread, nullptr, 0, nullptr) == nullptr) {
                OutputDebugStringW(L"[fx-hook] Failed to create hook worker thread\n");
            }
            if (CreateThread(nullptr, 0, UpdateCheckThread, nullptr, 0, nullptr) == nullptr) {
                OutputDebugStringW(L"[fx-hook] Failed to create update check thread\n");
            }
            break;
        case DLL_PROCESS_DETACH:
            StopModuleWatcher();
            break;
        default:
            break;
    }

    return TRUE;
}
