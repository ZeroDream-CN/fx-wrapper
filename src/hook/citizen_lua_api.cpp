#include "citizen_lua_api.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kTValueSize = 0x20;
constexpr int kTypeOffset = 0x10;
constexpr std::size_t kSandboxExecuteScanSize = 512;

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

bool MatchesSystemLibsEntry(
    HMODULE module,
    const std::uintptr_t* entry,
    const char* const expectedNames[],
    int expectedCount) {
    for (int index = 0; index < expectedCount; ++index) {
        const char* name = reinterpret_cast<const char*>(entry[index * 2]);
        if (!IsAddressInsideModule(module, name)) {
            return false;
        }

        if (std::strcmp(name, expectedNames[index]) != 0) {
            return false;
        }
    }

    return true;
}

void* FindCallAfterStringReference(const void* function, const char* stringAddress, std::size_t scanSize) {
    if (function == nullptr || stringAddress == nullptr) {
        return nullptr;
    }

    const auto bytes = reinterpret_cast<const std::uint8_t*>(function);
    const std::uintptr_t functionAddress = reinterpret_cast<std::uintptr_t>(function);
    const std::uintptr_t targetString = reinterpret_cast<std::uintptr_t>(stringAddress);

    for (std::size_t index = 0; index + 7 < scanSize; ++index) {
        if (bytes[index] != 0x48 || bytes[index + 1] != 0x8D) {
            continue;
        }

        const std::uint8_t modrm = bytes[index + 2];
        if ((modrm & 0xC7) != 0x05) {
            continue;
        }

        const std::int32_t displacement = *reinterpret_cast<const std::int32_t*>(bytes + index + 3);
        const std::uintptr_t referencedAddress = functionAddress + index + 7 + displacement;
        if (referencedAddress != targetString) {
            continue;
        }

        for (std::size_t callIndex = index; callIndex < index + 32 && callIndex + 5 < scanSize; ++callIndex) {
            if (bytes[callIndex] != 0xE8) {
                continue;
            }

            const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(bytes + callIndex + 1);
            return reinterpret_cast<void*>(functionAddress + callIndex + 5 + relative);
        }
    }

    return nullptr;
}

std::uint8_t** GetTopSlotPointer(lua_State* L) {
    return reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(L) + 0x10);
}

std::uint8_t* GetTopSlot(lua_State* L) {
    return *GetTopSlotPointer(L);
}

void AdvanceTop(lua_State* L, int slots = 1) {
    *GetTopSlotPointer(L) += kTValueSize * slots;
}

std::uint8_t GetLuaTypeTag(const std::uint8_t* slot) {
    return slot[kTypeOffset] & 0x0F;
}

const char* ReadStringFromSlot(const std::uint8_t* slot) {
    if (slot == nullptr) {
        return nullptr;
    }

    const auto stringObject = *reinterpret_cast<const std::uint8_t* const*>(slot);
    if (stringObject == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<const char*>(stringObject + 0x18);
}

const char* ReadStringFromTopRelative(lua_State* L, int slotOffsetFromTop) {
    const std::uint8_t* slot = GetTopSlot(L) + (slotOffsetFromTop * kTValueSize);
    if (GetLuaTypeTag(slot) != 5) {
        return nullptr;
    }

    return ReadStringFromSlot(slot);
}

}  // namespace

bool ResolveCitizenLuaApi(HMODULE module, void* sandboxExecute, CitizenLuaApi& outApi) {
    const char* permissionDenied = FindNullTerminatedString(module, "Permission denied");
    void* pushString = FindCallAfterStringReference(sandboxExecute, permissionDenied, kSandboxExecuteScanSize);
    if (pushString == nullptr) {
        return false;
    }

    outApi.lua_pushstring = reinterpret_cast<CitizenLuaApi::LuaPushStringFn>(pushString);
    return IsAddressInsideModule(module, reinterpret_cast<const void*>(outApi.lua_pushstring));
}

void* FindSystemLibsFunction(HMODULE module, const char* functionName) {
    if (functionName == nullptr) {
        return nullptr;
    }

    const char* clockName = FindNullTerminatedString(module, "clock");
    if (clockName == nullptr) {
        return nullptr;
    }

    static const char* kExpectedNames[] = {"clock", "date", "difftime", "execute"};

    const auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const std::uint8_t*>(module) + dosHeader->e_lfanew);
    const std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;
    const auto imageBase = reinterpret_cast<const std::uint8_t*>(module);
    const std::uintptr_t clockPointer = reinterpret_cast<std::uintptr_t>(clockName);

    for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= imageSize; offset += sizeof(std::uintptr_t)) {
        if (*reinterpret_cast<const std::uintptr_t*>(imageBase + offset) != clockPointer) {
            continue;
        }

        const auto entry = reinterpret_cast<const std::uintptr_t*>(imageBase + offset);
        if (!MatchesSystemLibsEntry(module, entry, kExpectedNames, 4)) {
            continue;
        }

        for (int index = 0; index < 64; ++index) {
            const char* name = reinterpret_cast<const char*>(entry[index * 2]);
            if (name == nullptr || !IsAddressInsideModule(module, name)) {
                break;
            }

            if (std::strcmp(name, functionName) == 0) {
                return reinterpret_cast<void*>(entry[index * 2 + 1]);
            }
        }
    }

    return nullptr;
}

const char* ReadLuaStringArgFromTop(lua_State* L, int offsetFromTop) {
    if (L == nullptr) {
        return nullptr;
    }

    const std::uint8_t* slot = GetTopSlot(L) + (offsetFromTop * kTValueSize);
    if (GetLuaTypeTag(slot) != 5) {
        return nullptr;
    }

    return ReadStringFromSlot(slot);
}

const char* ReadLuaOptStringArg1(lua_State* L) {
    if (L == nullptr) {
        return nullptr;
    }

    for (int offset = -1; offset >= -3; --offset) {
        const std::uint8_t* slot = GetTopSlot(L) + (offset * kTValueSize);
        const std::uint8_t typeTag = GetLuaTypeTag(slot);
        if (typeTag == 0) {
            return nullptr;
        }

        if (typeTag != 5) {
            continue;
        }

        return ReadStringFromSlot(slot);
    }

    return ReadStringFromTopRelative(L, -1);
}

void PushLuaBoolean(lua_State* L, bool value) {
    std::uint8_t* slot = GetTopSlot(L);
    *reinterpret_cast<std::uint64_t*>(slot) = value ? 1ULL : 0ULL;
    slot[kTypeOffset] = 0x11;
    AdvanceTop(L);
}

void PushLuaNil(lua_State* L) {
    std::uint8_t* slot = GetTopSlot(L);
    *reinterpret_cast<std::uint64_t*>(slot) = 0;
    slot[kTypeOffset] = 0;
    AdvanceTop(L);
}

void PushLuaInteger(lua_State* L, long long value) {
    std::uint8_t* slot = GetTopSlot(L);
    *reinterpret_cast<std::uint64_t*>(slot) = static_cast<std::uint64_t>(value);
    slot[kTypeOffset] = 0x03;
    AdvanceTop(L);
}

void PushLuaString(CitizenLuaApi& api, lua_State* L, const char* value) {
    AdvanceTop(L);
    api.lua_pushstring(L, value);
}

int PushLuaExecResult(CitizenLuaApi& api, lua_State* L, int stat) {
    if (stat != -1) {
        if ((stat & 0xFF) == 0) {
            PushLuaBoolean(L, true);
            return 1;
        }

        PushLuaNil(L);
        PushLuaString(api, L, "exit");
        PushLuaInteger(L, stat);
        return 3;
    }

    PushLuaNil(L);
    PushLuaString(api, L, std::strerror(errno));
    PushLuaInteger(L, errno);
    return 3;
}

int PushLuaFileResult(CitizenLuaApi& api, lua_State* L, bool success, const char* path) {
    if (success) {
        PushLuaBoolean(L, true);
        return 1;
    }

    PushLuaNil(L);
    char message[512];
    std::snprintf(message, sizeof(message), "%s: %s", path, std::strerror(errno));
    PushLuaString(api, L, message);
    PushLuaInteger(L, errno);
    return 3;
}

int PushLuaRenameResult(CitizenLuaApi& api, lua_State* L, bool success, const char* from, const char* to) {
    if (success) {
        PushLuaBoolean(L, true);
        return 1;
    }

    PushLuaNil(L);
    char message[512];
    std::snprintf(message, sizeof(message), "%s -> %s: %s", from, to, std::strerror(errno));
    PushLuaString(api, L, message);
    PushLuaInteger(L, errno);
    return 3;
}
