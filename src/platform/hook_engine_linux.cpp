#include "platform/platform.h"

#include <subhook.h>

#include <vector>

namespace {

std::vector<subhook_t> g_installedSubhooks;

}  // namespace

bool EnsureHookEngineInitialized() {
    return true;
}

bool CreateAndEnableHook(void* target, void* detour, void** original, const char* symbolName) {
    if (target == nullptr) {
        DebugLog("[fx-hook] Export not found: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    subhook_t hook = subhook_new(target, detour, SUBHOOK_64BIT_OFFSET);
    if (hook == nullptr) {
        DebugLog("[fx-hook] subhook_new failed: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    if (subhook_install(hook) != 0) {
        subhook_free(hook);
        DebugLog("[fx-hook] subhook_install failed: ");
        DebugLog(symbolName);
        DebugLogLine("");
        return false;
    }

    g_installedSubhooks.push_back(hook);

    if (original != nullptr) {
        *original = subhook_get_trampoline(hook);
    }

    DebugLog("[fx-hook] Hook installed: ");
    DebugLog(symbolName);
    DebugLogLine("");
    return true;
}
