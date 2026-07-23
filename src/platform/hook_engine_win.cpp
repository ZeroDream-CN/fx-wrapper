#include "platform/platform.h"

#include <MinHook.h>

bool EnsureHookEngineInitialized() {
    static bool initialized = false;
    if (initialized) {
        return true;
    }

    if (MH_Initialize() != MH_OK) {
        DebugLogLine("[fx-hook] MH_Initialize failed");
        return false;
    }

    initialized = true;
    return true;
}

bool CreateAndEnableHook(void* target, void* detour, void** original, const char* symbolName) {
    if (target == nullptr) {
        DebugLog("[fx-hook] Export not found: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    if (MH_CreateHook(target, detour, original) != MH_OK) {
        DebugLog("[fx-hook] MH_CreateHook failed: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        DebugLog("[fx-hook] MH_EnableHook failed: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    DebugLog("[fx-hook] Hook installed: ");
    DebugLog(symbolName);
    DebugLogLine("");
    return true;
}
