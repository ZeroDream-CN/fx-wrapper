#include "node_hooks.h"

#include "fx_log.h"
#include "hook_util.h"

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

constexpr char kScriptingNodeModule[] = "citizen-scripting-node.dll";
constexpr char kNodePermissionCallbackSymbol[] = "NodePermissionCallback";

using NodePermissionCallbackFn = bool(__fastcall*)(void* self, void* env, int permission, const void* resource);

NodePermissionCallbackFn g_originalNodePermissionCallback = nullptr;

std::atomic<bool> g_nodeHookInstalled{false};
std::atomic<bool> g_nodeHookScheduled{false};

constexpr int kHookInstallAttempts = 600;
constexpr DWORD kHookInstallInitialDelayMs = 500;
constexpr DWORD kHookInstallRetryDelayMs = 100;

bool IsAddressInsideModule(HMODULE module, const void* address) {
    if (module == nullptr || address == nullptr) {
        return false;
    }

    const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
    const std::uintptr_t end = base + ntHeaders->OptionalHeader.SizeOfImage;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
    return value >= base && value < end;
}

const char* FindNullTerminatedString(HMODULE module, const char* needle) {
    if (module == nullptr || needle == nullptr) {
        return nullptr;
    }

    const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dosHeader->e_lfanew);
    const std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
    const auto imageBase = reinterpret_cast<const std::uint8_t*>(module);
    const std::size_t needleLength = std::strlen(needle);

    for (std::size_t offset = 0; offset + needleLength + 1 <= imageSize; ++offset) {
        if (std::memcmp(imageBase + offset, needle, needleLength) != 0) {
            continue;
        }

        if (imageBase[offset + needleLength] != '\0') {
            continue;
        }

        return reinterpret_cast<const char*>(imageBase + offset);
    }

    return nullptr;
}

void* FindLeaTargetFunctionStart(HMODULE module, const char* markerString) {
    const char* marker = FindNullTerminatedString(module, markerString);
    if (marker == nullptr) {
        return nullptr;
    }

    const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dosHeader->e_lfanew);
    const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(module);
    const std::uintptr_t markerAddress = reinterpret_cast<std::uintptr_t>(marker);

    const WORD sectionCount = ntHeaders->FileHeader.NumberOfSections;
    const auto sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

    for (WORD sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        const IMAGE_SECTION_HEADER& section = sectionHeader[sectionIndex];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }

        const auto bytes = reinterpret_cast<const std::uint8_t*>(module) + section.VirtualAddress;
        const std::size_t sectionSize = section.Misc.VirtualSize;

        for (std::size_t index = 0; index + 7 < sectionSize; ++index) {
            if (bytes[index + 1] != 0x8D || (bytes[index] != 0x48 && bytes[index] != 0x4C)) {
                continue;
            }

            const std::uint8_t modrm = bytes[index + 2];
            if ((modrm & 0xC7) != 0x05) {
                continue;
            }

            const std::int32_t displacement = *reinterpret_cast<const std::int32_t*>(bytes + index + 3);
            const std::uintptr_t instructionAddress = imageBase + section.VirtualAddress + index;
            const std::uintptr_t referencedAddress = instructionAddress + 7 + displacement;
            if (referencedAddress != markerAddress) {
                continue;
            }

            const std::size_t instructionOffset = section.VirtualAddress + index;
            for (std::size_t back = 0; back < 0x800; ++back) {
                if (instructionOffset < back) {
                    break;
                }

                const std::size_t candidateOffset = instructionOffset - back;
                const std::uint8_t* candidate = reinterpret_cast<const std::uint8_t*>(module) + candidateOffset;
                if (candidate[0] == 0x48 && candidate[1] == 0x89 && candidate[2] == 0x5C && candidate[3] == 0x24 &&
                    candidate[4] == 0x10 && candidate[5] == 0x55) {
                    return reinterpret_cast<void*>(imageBase + candidateOffset);
                }
            }
        }
    }

    return nullptr;
}

void* FindNodePermissionCallback(HMODULE module) {
    void* fromYarn = FindLeaTargetFunctionStart(module, "yarn");
    void* fromWebpack = FindLeaTargetFunctionStart(module, "webpack");
    if (fromYarn == nullptr || fromWebpack == nullptr || fromYarn != fromWebpack) {
        return nullptr;
    }

    return fromYarn;
}

bool __fastcall Hook_NodePermissionCallback(void* /*self*/, void* /*env*/, int /*permission*/, const void* /*resource*/) {
    return true;
}

bool InstallNodePermissionHookOnce() {
    if (g_nodeHookInstalled.load()) {
        return true;
    }

    HMODULE module = GetModuleHandleA(kScriptingNodeModule);
    if (module == nullptr) {
        return false;
    }

    void* target = FindNodePermissionCallback(module);
    if (target == nullptr || !IsAddressInsideModule(module, target)) {
        return false;
    }

    if (!EnsureMinHookInitialized()) {
        return false;
    }

    if (!CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&Hook_NodePermissionCallback),
            reinterpret_cast<void**>(&g_originalNodePermissionCallback),
            kNodePermissionCallbackSymbol)) {
        return false;
    }

    g_nodeHookInstalled.store(true);
    OutputDebugStringW(L"[fx-hook] NodePermissionCallback hook installed\n");
    NotifyHookStageInstalled(HookInstallStage::ScriptingNode);
    return true;
}

void NodeHookWorker() {
    Sleep(kHookInstallInitialDelayMs);

    for (int attempt = 0; attempt < kHookInstallAttempts; ++attempt) {
        if (InstallNodePermissionHookOnce()) {
            return;
        }

        Sleep(kHookInstallRetryDelayMs);
    }

    OutputDebugStringW(L"[fx-hook] Timed out installing NodePermissionCallback hook\n");
}

}  // namespace

void ScheduleNodePermissionHookInstall() {
    bool expected = false;
    if (!g_nodeHookScheduled.compare_exchange_strong(expected, true)) {
        return;
    }

    std::thread(NodeHookWorker).detach();
}
