#include "module_watcher.h"

#include "platform/platform.h"

#if !defined(_WIN32)
#include "platform/linux_thread.h"
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace {

std::function<void()> g_onModuleLoaded;
std::atomic<bool> g_moduleHandled{false};
std::atomic<bool> g_stopRequested{false};
std::thread g_pollThread;

constexpr std::uint32_t kPollIntervalMs = 100;
constexpr std::uint32_t kPollTimeoutMs = 300'000;

bool IsScriptingCoreAvailable() {
    return GetModuleHandleByName(ScriptingCoreModuleName()) != nullptr ||
        IsModuleMapped("citizen-scripting-core");
}

void InvokeModuleLoadedOnce() {
    bool expected = false;
    if (!g_moduleHandled.compare_exchange_strong(expected, true)) {
        return;
    }

    PlatformSleepMs(200);

    if (g_onModuleLoaded) {
        g_onModuleLoaded();
    }
}

void PollForTargetModule() {
    const auto startTick = std::chrono::steady_clock::now();
    while (!g_stopRequested.load() && !g_moduleHandled.load()) {
        if (IsScriptingCoreAvailable()) {
            DebugLogLine("[fx-hook] Scripting core module detected");
            InvokeModuleLoadedOnce();
            return;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTick);
        if (elapsed.count() >= kPollTimeoutMs) {
            DebugLogLine("[fx-hook] Timed out waiting for scripting core module");
            return;
        }

        PlatformSleepMs(kPollIntervalMs);
    }
}

static void PollForTargetModuleEntry() {
    PollForTargetModule();
}

}  // namespace

void StartModuleWatcher(std::function<void()> onModuleLoaded) {
    g_onModuleLoaded = std::move(onModuleLoaded);
    g_moduleHandled.store(false);
    g_stopRequested.store(false);

    if (IsScriptingCoreAvailable()) {
        InvokeModuleLoadedOnce();
        return;
    }

#if defined(_WIN32)
    g_pollThread = std::thread(PollForTargetModule);
#else
    LaunchDetachedThread(PollForTargetModuleEntry);
#endif
}

void StopModuleWatcher() {
    g_stopRequested.store(true);

    if (g_pollThread.joinable()) {
        g_pollThread.join();
    }

    g_onModuleLoaded = nullptr;
}
