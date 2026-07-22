#include "fx_log.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr char kLogChannel[] = "fx-wrapper";
constexpr char kHooksInstalledMessage[] = "FXServer Wrapper by Akkariin\nAll hooks installed, sandbox features bypassed\n";

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
    static PrintfvFn printfv = []() -> PrintfvFn {
        HMODULE coreRt = GetModuleHandleW(L"CoreRT.dll");
        if (coreRt == nullptr) {
            return nullptr;
        }

        return reinterpret_cast<PrintfvFn>(GetProcAddress(coreRt, "Printfv"));
    }();

    return printfv;
}

ConHostPrintFn ResolveConHostPrint() {
    static ConHostPrintFn print = []() -> ConHostPrintFn {
        HMODULE conhost = GetModuleHandleW(L"conhost-server.dll");
        if (conhost == nullptr) {
            return nullptr;
        }

        return reinterpret_cast<ConHostPrintFn>(GetProcAddress(
            conhost,
            "?Print@ConHost@@YAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"));
    }();

    return print;
}

void WriteFallbackLog(const char* message) {
    OutputDebugStringA("[fx-hook] ");
    OutputDebugStringA(message);
}

void WriteCoreLog(const char* channel, const char* message) {
    PrintfvFn printfv = ResolvePrintfv();
    if (printfv != nullptr) {
        FmtPrintfArgs args{};
        printfv(std::string(channel), std::string_view(message, std::strlen(message)), args);
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
