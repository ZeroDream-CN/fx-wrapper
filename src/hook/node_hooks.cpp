#include "node_hooks.h"

#include "binary_module.h"
#include "fx_log.h"
#include "hook_util.h"
#include "platform/platform.h"

#if !defined(_WIN32)
#include "platform/linux_thread.h"
#endif

#include <atomic>
#include <thread>
#include <vector>

namespace {

constexpr char kNodePermissionCallbackSymbol[] = "NodePermissionCallback";

std::atomic<bool> g_nodeHookInstalled{false};
std::atomic<bool> g_nodeHookScheduled{false};

constexpr int kHookInstallAttempts = 600;
constexpr std::uint32_t kHookInstallInitialDelayMs = 500;
constexpr std::uint32_t kHookInstallRetryDelayMs = 100;

#if defined(_WIN32)

using NodePermissionCallbackFn = bool (*)(void* self, void* env, int permission, const void* resource);

NodePermissionCallbackFn g_originalNodePermissionCallback = nullptr;

bool Hook_NodePermissionCallback(void* /*self*/, void* /*env*/, int /*permission*/, const void* /*resource*/) {
    return true;
}

void* FindNodePermissionCallback(const ModuleImage& image) {
    void* fromBoth = FindFunctionContainingBothStringMarkers(image, "yarn", "webpack");
    if (fromBoth != nullptr) {
        return fromBoth;
    }

    void* fromChildSpawn = FindFunctionContainingCStringFragment(image, "child spawn not allowed");
    if (fromChildSpawn != nullptr) {
        return fromChildSpawn;
    }

    return FindFunctionContainingStringMarker(image, "webpack");
}

#else

constexpr char kSetPermissionHandlerSymbol[] =
    "_ZN4node20SetPermissionHandlerEPNS_11EnvironmentEOSt8functionIFbS1_NS_10permission15PermissionScopeERKSt17basic_string_viewIcSt11char_traitsIcEEEE";

using SetPermissionHandlerFn = void (*)(void* env, void* handler);

SetPermissionHandlerFn g_originalSetPermissionHandler = nullptr;

void Hook_SetPermissionHandler(void* /*env*/, void* /*handler*/) {
}

void* ResolveSetPermissionHandler(ModuleHandle module) {
    std::vector<const char*> symbols = {kSetPermissionHandlerSymbol};
    void* symbol = GetModuleSymbol(module, symbols);
    if (symbol != nullptr) {
        return symbol;
    }

    return GetModuleSymbol(nullptr, symbols);
}

#endif

bool InstallNodePermissionHookOnce() {
    if (g_nodeHookInstalled.load()) {
        return true;
    }

    ModuleHandle module = GetModuleHandleByName(ScriptingNodeModuleName());
    if (module == nullptr && !IsModuleMapped("citizen-scripting-node")) {
        return false;
    }

    if (!EnsureHookEngineInitialized()) {
        return false;
    }

#if defined(_WIN32)

    ModuleImage image{};
    if (!GetModuleImage(module, image) && !GetModuleImageByFragment("citizen-scripting-node", image)) {
        return false;
    }

    void* target = FindNodePermissionCallback(image);
    if (target == nullptr || !IsAddressExecutable(image, target)) {
        return false;
    }

    if (!CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&Hook_NodePermissionCallback),
            reinterpret_cast<void**>(&g_originalNodePermissionCallback),
            kNodePermissionCallbackSymbol)) {
        return false;
    }

#else

    void* target = ResolveSetPermissionHandler(module);
    if (target == nullptr) {
        return false;
    }

    if (!CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&Hook_SetPermissionHandler),
            reinterpret_cast<void**>(&g_originalSetPermissionHandler),
            "SetPermissionHandler")) {
        return false;
    }

#endif

    g_nodeHookInstalled.store(true);
    DebugLogLine("[fx-hook] NodePermissionCallback hook installed");
    NotifyHookStageInstalled(HookInstallStage::ScriptingNode);
    return true;
}

void NodeHookWorker() {
    PlatformSleepMs(kHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kHookInstallAttempts; ++attempt) {
        if (InstallNodePermissionHookOnce()) {
            return;
        }

        PlatformSleepMs(kHookInstallRetryDelayMs);
    }

    DebugLogLine("[fx-hook] Timed out installing NodePermissionCallback hook");
}

static void NodeHookWorkerEntry() {
    NodeHookWorker();
}

}  // namespace

void ScheduleNodePermissionHookInstall() {
    bool expected = false;
    if (!g_nodeHookScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

#if defined(_WIN32)
    std::thread(NodeHookWorker).detach();
#else
    LaunchDetachedThread(NodeHookWorkerEntry);
#endif
}
