#include "fx_log.h"

#include <windows.h>

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

using PrintfvFn = void(__cdecl*)(std::string channel, std::string_view format, FmtPrintfArgs args);
using ConHostPrintFn = void(__cdecl*)(const std::string& channel, const std::string& message);

std::atomic<uint8_t> g_installedStages{0};
std::atomic<bool> g_hooksInstalledLogged{false};

constexpr uint8_t StageBit(HookInstallStage stage) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(stage));
}

constexpr uint8_t kAllStages =
    StageBit(HookInstallStage::ProcessSpawn) | StageBit(HookInstallStage::ScriptingCore) |
    StageBit(HookInstallStage::LuaOs);

PrintfvFn ResolvePrintfv() {
    static PrintfvFn cached = nullptr;
    if (cached != nullptr) {
        return cached;
    }

    HMODULE coreRt = GetModuleHandleW(L"CoreRT.dll");
    if (coreRt == nullptr) {
        return nullptr;
    }

    cached = reinterpret_cast<PrintfvFn>(GetProcAddress(coreRt, "Printfv"));
    return cached;
}

ConHostPrintFn ResolveConHostPrint() {
    static ConHostPrintFn cached = nullptr;
    if (cached != nullptr) {
        return cached;
    }

    HMODULE conhost = GetModuleHandleW(L"conhost-server.dll");
    if (conhost == nullptr) {
        return nullptr;
    }

    cached = reinterpret_cast<ConHostPrintFn>(GetProcAddress(
        conhost,
        "?Print@ConHost@@YAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"));
    return cached;
}

void WriteFallbackLog(const char* message) {
    OutputDebugStringA("[fx-hook] ");
    OutputDebugStringA(message);
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
        printfv(
            std::string(channel),
            std::string_view(escaped.data(), escaped.size()),
            args);
        return;
    }

    ConHostPrintFn conHostPrint = ResolveConHostPrint();
    if (conHostPrint != nullptr) {
        conHostPrint(std::string(channel), std::string(message));
        return;
    }

    WriteFallbackLog(message);
}

void TryLogHooksInstalled() {
    if (g_installedStages.load(std::memory_order_acquire) != kAllStages) {
        return;
    }

    bool expected = false;
    if (!g_hooksInstalledLogged.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

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
    constexpr DWORD kPollIntervalMs = 50;
    DWORD waited = 0;

    while (waited < maxWaitMs) {
        if (ResolvePrintfv() != nullptr) {
            return true;
        }

        Sleep(kPollIntervalMs);
        waited += kPollIntervalMs;
    }

    return ResolvePrintfv() != nullptr;
}
