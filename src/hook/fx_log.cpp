#include "fx_log.h"

#include "platform/platform.h"

#include <atomic>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr char kLogChannel[] = "fx-wrapper";
constexpr char kHooksInstalledMessage[] = "^1FXServer ^0Wrapper by ^3Akkariin^0\n^2Hook 安装成功，沙盒功能已绕过^0\n";

struct FmtPrintfArgs {
    alignas(16) unsigned char data[64]{};
};

using PrintfvFn = void (*)(std::string channel, std::string_view format, FmtPrintfArgs args);

#if defined(_WIN32)
using ConHostPrintFn = void (*)(const std::string& channel, const std::string& message);
#endif

std::atomic<uint8_t> g_installedStages{0};
std::atomic<bool> g_hooksInstalledLogged{false};

constexpr uint8_t StageBit(HookInstallStage stage) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(stage));
}

constexpr uint8_t kAllStages =
    StageBit(HookInstallStage::ProcessSpawn) | StageBit(HookInstallStage::ScriptingCore) |
    StageBit(HookInstallStage::ScriptingNode) | StageBit(HookInstallStage::LuaOs);

PrintfvFn ResolvePrintfv() {
    static PrintfvFn cached = nullptr;
    if (cached != nullptr) {
        return cached;
    }

    ModuleHandle coreRt = GetModuleHandleByName(CoreRuntimeModuleName());
    if (coreRt == nullptr) {
        return nullptr;
    }

    cached = reinterpret_cast<PrintfvFn>(GetModuleSymbol(coreRt, "Printfv"));
    return cached;
}

#if defined(_WIN32)
ConHostPrintFn ResolveConHostPrint() {
    static ConHostPrintFn cached = nullptr;
    if (cached != nullptr) {
        return cached;
    }

    ModuleHandle conhost = GetModuleHandleByName("conhost-server.dll");
    if (conhost == nullptr) {
        return nullptr;
    }

    cached = reinterpret_cast<ConHostPrintFn>(GetModuleSymbol(
        conhost,
        "?Print@ConHost@@YAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"));
    return cached;
}
#endif

void WriteFallbackLog(const char* message) {
    DebugLog("[fx-hook] ");
    DebugLog(message);
}

std::string EscapePrintfLiteral(const char* message) {
    std::string escaped;
    if (message == nullptr) {
        return escaped;
    }

    escaped.reserve(std::strlen(message) + 8);
    for (const char* cursor = message; *cursor != '\0'; ++cursor) {
        if (*cursor == '%') {
            escaped.append("%%");
        } else {
            escaped.push_back(*cursor);
        }
    }

    return escaped;
}

void WriteCoreLog(const char* channel, const char* message) {
    PrintfvFn printfv = ResolvePrintfv();
    if (printfv != nullptr) {
        const std::string escaped = EscapePrintfLiteral(message);
        FmtPrintfArgs args{};
        printfv(std::string(channel), std::string_view(escaped.data(), escaped.size()), args);
        return;
    }

#if defined(_WIN32)
    ConHostPrintFn conHostPrint = ResolveConHostPrint();
    if (conHostPrint != nullptr) {
        conHostPrint(std::string(channel), std::string(message));
        return;
    }
#endif

    WriteFallbackLog(message);
    DebugLogLine("");
}

void TryLogHooksInstalled() {
    if (g_installedStages.load(std::memory_order_acquire) != kAllStages) {
        return;
    }

    bool expected = false;
    if (!g_hooksInstalledLogged.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    WaitForLogReady();
    WriteCoreLog(kLogChannel, kHooksInstalledMessage);
}

}  // namespace

void NotifyHookStageInstalled(HookInstallStage stage) {
    g_installedStages.fetch_or(StageBit(stage), std::memory_order_release);
    TryLogHooksInstalled();
}

void LogFxMessage(const char* message) {
    if (message == nullptr || message[0] == '\0') {
        return;
    }

    WriteCoreLog(kLogChannel, message);
}

bool WaitForLogReady(std::uint32_t maxWaitMs) {
    constexpr std::uint32_t kPollIntervalMs = 50;
    std::uint32_t waited = 0;

    while (waited < maxWaitMs) {
        if (ResolvePrintfv() != nullptr) {
            return true;
        }

        PlatformSleepMs(kPollIntervalMs);
        waited += kPollIntervalMs;
    }

    return ResolvePrintfv() != nullptr;
}
