#include "citizen_lua_api.h"

#include "binary_module.h"
#include "platform/platform.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kTValueSize = 0x20;
constexpr int kTypeOffset = 0x10;
constexpr std::size_t kSandboxExecuteScanSize = 512;

void* FindCallAfterStringReference(const void* function, const char* stringAddress, std::size_t scanSize) {
    if (function == nullptr || stringAddress == nullptr) {
        return nullptr;
    }

    const auto bytes = reinterpret_cast<const std::uint8_t*>(function);
    const std::uintptr_t functionAddress = reinterpret_cast<std::uintptr_t>(function);
    const std::uintptr_t targetString = reinterpret_cast<std::uintptr_t>(stringAddress);

    for (std::size_t index = 0; index + 7 < scanSize; ++index) {
        std::uintptr_t referencedAddress = 0;
        if (!TryDecodeRipRelativeReference(bytes + index, scanSize - index, functionAddress + index, targetString,
                referencedAddress)) {
            continue;
        }

        for (std::size_t callIndex = index; callIndex < index + 32 && callIndex + 5 < scanSize; ++callIndex) {
            if (bytes[callIndex] == 0xE8) {
                const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(bytes + callIndex + 1);
                return reinterpret_cast<void*>(functionAddress + callIndex + 5 + relative);
            }

            if (callIndex + 6 <= scanSize && bytes[callIndex] == 0x67 && bytes[callIndex + 1] == 0xE8) {
                const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(bytes + callIndex + 2);
                return reinterpret_cast<void*>(functionAddress + callIndex + 6 + relative);
            }
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

constexpr std::size_t kCitizenLuaTableEntrySize = 48;
constexpr std::uintptr_t kCitizenLuaTableMetaMarker = 8;
constexpr char kLuaFxOpenOsSymbol[] = "_ZN2fx13lua_fx_openosEP9lua_State";
constexpr std::size_t kOpenOsScanSize = 128;

bool LooksLikeSystemLibsTable(const ModuleImage& image, const void* table) {
    if (table == nullptr) {
        return false;
    }

    const auto* entry = reinterpret_cast<const std::uintptr_t*>(table);
    if (entry[0] == 0) {
        return false;
    }

    const char* firstName = reinterpret_cast<const char*>(entry[0]);
    if (!IsAddressInsideModule(image, firstName)) {
        return false;
    }

    return std::strcmp(firstName, "clock") == 0;
}

void* FindInLuaRegTable(const ModuleImage& image, const void* table, const char* functionName) {
    if (table == nullptr || functionName == nullptr) {
        return nullptr;
    }

    const auto* entry = reinterpret_cast<const std::uintptr_t*>(table);
    for (int index = 0; index < 64; ++index, entry += 2) {
        if (entry[0] == 0) {
            break;
        }

        const char* entryName = reinterpret_cast<const char*>(entry[0]);
        if (!IsAddressInsideModule(image, entryName)) {
            break;
        }

        void* function = reinterpret_cast<void*>(entry[1]);
        if (function == nullptr) {
            break;
        }

        if (std::strcmp(entryName, functionName) != 0) {
            continue;
        }

        if (!IsAddressInsideModule(image, function) || !IsAddressExecutable(image, function)) {
            return nullptr;
        }

        return function;
    }

    return nullptr;
}

void* FindSystemLibsTableFromOpenOs(const ModuleImage& image) {
    ModuleHandle module = image.handle;
    if (module == nullptr) {
        module = GetModuleHandleByName(ScriptingLuaModuleName());
    }

    if (module == nullptr) {
        return nullptr;
    }

    void* openOs = GetModuleSymbol(module, kLuaFxOpenOsSymbol);
    if (openOs == nullptr || !IsAddressExecutable(image, openOs)) {
        return nullptr;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(openOs);
    const std::uintptr_t functionAddress = reinterpret_cast<std::uintptr_t>(openOs);
    for (std::size_t index = 0; index + 7 < kOpenOsScanSize; ++index) {
        std::uintptr_t referencedAddress = 0;
        if (!TryDecodeRipRelativeLea(
                bytes + index, kOpenOsScanSize - index, functionAddress + index, referencedAddress)) {
            continue;
        }

        void* table = reinterpret_cast<void*>(referencedAddress);
        if (!IsAddressInsideModule(image, table) || IsAddressExecutable(image, table)) {
            continue;
        }

        if (LooksLikeSystemLibsTable(image, table)) {
            return table;
        }
    }

    return nullptr;
}

void* FindClassicLuaRegFunction(const ModuleImage& image, const char* functionName) {
    if (functionName == nullptr) {
        return nullptr;
    }

    const char* nameString = FindNullTerminatedString(image, functionName);
    if (nameString == nullptr) {
        return nullptr;
    }

    const std::uintptr_t namePointer = reinterpret_cast<std::uintptr_t>(nameString);

    auto scanSegment = [&](const std::uint8_t* base, std::size_t size) -> void* {
        if (base == nullptr || size < (2 * sizeof(std::uintptr_t))) {
            return nullptr;
        }

        for (std::size_t offset = 0; offset + (2 * sizeof(std::uintptr_t)) <= size;
             offset += sizeof(std::uintptr_t)) {
            const auto* entry = reinterpret_cast<const std::uintptr_t*>(base + offset);
            if (entry[0] != namePointer) {
                continue;
            }

            void* function = reinterpret_cast<void*>(entry[1]);
            if (!IsAddressInsideModule(image, function) || !IsAddressExecutable(image, function)) {
                continue;
            }

            const char* entryName = reinterpret_cast<const char*>(entry[0]);
            if (!IsAddressInsideModule(image, entryName) || std::strcmp(entryName, functionName) != 0) {
                continue;
            }

            return function;
        }

        return nullptr;
    };

    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (void* found = scanSegment(segment.base, segment.size)) {
                return found;
            }
        }

        return nullptr;
    }

    if (image.base == nullptr || image.size == 0) {
        return nullptr;
    }

    return scanSegment(image.base, image.size);
}

bool LooksLikeCitizenLuaTableEntry(const std::uintptr_t* entry) {
    return entry[2] == kCitizenLuaTableMetaMarker && entry[5] == kCitizenLuaTableMetaMarker;
}

void* FindCitizenLuaTableFunction(const ModuleImage& image, const char* functionName) {
    if (functionName == nullptr) {
        return nullptr;
    }

    const char* nameString = FindNullTerminatedString(image, functionName);
    if (nameString == nullptr) {
        return nullptr;
    }

    const std::uintptr_t namePointer = reinterpret_cast<std::uintptr_t>(nameString);
    void* bestMatch = nullptr;

    auto considerEntry = [&](const std::uintptr_t* entry) -> void* {
        if (entry[0] != namePointer) {
            return nullptr;
        }

        if (!LooksLikeCitizenLuaTableEntry(entry)) {
            return nullptr;
        }

        void* function = reinterpret_cast<void*>(entry[3]);
        if (!IsAddressInsideModule(image, function) || !IsAddressExecutable(image, function)) {
            return nullptr;
        }

        const char* entryName = reinterpret_cast<const char*>(entry[0]);
        if (!IsAddressInsideModule(image, entryName) || std::strcmp(entryName, functionName) != 0) {
            return nullptr;
        }

        return function;
    };

    auto scanSegment = [&](const std::uint8_t* base, std::size_t size) {
        if (base == nullptr || size < kCitizenLuaTableEntrySize) {
            return;
        }

        for (std::size_t offset = 0; offset + kCitizenLuaTableEntrySize <= size; offset += sizeof(std::uintptr_t)) {
            const auto* entry = reinterpret_cast<const std::uintptr_t*>(base + offset);
            if (void* function = considerEntry(entry)) {
                if (std::strcmp(functionName, "execute") == 0 &&
                    FunctionReferencesString(image, function, "Permission denied", 0x200)) {
                    bestMatch = function;
                    return;
                }

                bestMatch = function;
            }
        }
    };

    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            scanSegment(segment.base, segment.size);
        }
    } else if (image.base != nullptr && image.size > 0) {
        scanSegment(image.base, image.size);
    }

    return bestMatch;
}

void* FindCitizenLuaTableSiblingFunction(
    const ModuleImage& image,
    void* knownFunction,
    const char* knownName,
    const char* targetName) {
    if (knownFunction == nullptr || knownName == nullptr || targetName == nullptr) {
        return nullptr;
    }

    const char* knownNameString = FindNullTerminatedString(image, knownName);
    const char* targetNameString = FindNullTerminatedString(image, targetName);
    if (knownNameString == nullptr || targetNameString == nullptr) {
        return nullptr;
    }

    const std::uintptr_t knownNamePointer = reinterpret_cast<std::uintptr_t>(knownNameString);
    const std::uintptr_t knownFunctionPointer = reinterpret_cast<std::uintptr_t>(knownFunction);
    const std::uintptr_t targetNamePointer = reinterpret_cast<std::uintptr_t>(targetNameString);

    auto scanSegment = [&](const std::uint8_t* base, std::size_t size) -> void* {
        if (base == nullptr || size < kCitizenLuaTableEntrySize) {
            return nullptr;
        }

        for (std::size_t offset = 0; offset + kCitizenLuaTableEntrySize <= size; offset += sizeof(std::uintptr_t)) {
            const auto* entry = reinterpret_cast<const std::uintptr_t*>(base + offset);
            if (entry[0] != knownNamePointer || entry[3] != knownFunctionPointer ||
                !LooksLikeCitizenLuaTableEntry(entry)) {
                continue;
            }

            for (int index = -8; index <= 32; ++index) {
                const std::size_t siblingOffset =
                    offset + (static_cast<std::size_t>(index) * kCitizenLuaTableEntrySize);
                if (siblingOffset + kCitizenLuaTableEntrySize > size) {
                    continue;
                }

                const auto* sibling = reinterpret_cast<const std::uintptr_t*>(base + siblingOffset);
                if (sibling[0] != targetNamePointer || !LooksLikeCitizenLuaTableEntry(sibling)) {
                    continue;
                }

                void* function = reinterpret_cast<void*>(sibling[3]);
                if (!IsAddressExecutable(image, function)) {
                    return nullptr;
                }

                return function;
            }
        }

        return nullptr;
    };

    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (void* found = scanSegment(segment.base, segment.size)) {
                return found;
            }
        }

        return nullptr;
    }

    if (image.base == nullptr || image.size == 0) {
        return nullptr;
    }

    return scanSegment(image.base, image.size);
}

}  // namespace

bool ResolveCitizenLuaApi(const ModuleImage& image, void* sandboxExecute, CitizenLuaApi& outApi) {
    const char* permissionDenied = FindNullTerminatedString(image, "Permission denied");
    void* pushString = FindCallAfterStringReference(sandboxExecute, permissionDenied, kSandboxExecuteScanSize);
    if (pushString == nullptr) {
        return false;
    }

    outApi.lua_pushstring = reinterpret_cast<CitizenLuaApi::LuaPushStringFn>(pushString);
    return IsAddressInsideModule(image, reinterpret_cast<const void*>(outApi.lua_pushstring));
}

void* FindSystemLibsFunctionFromOpenOs(const ModuleImage& image, const char* functionName) {
    void* table = FindSystemLibsTableFromOpenOs(image);
    if (table == nullptr) {
        return nullptr;
    }

    return FindInLuaRegTable(image, table, functionName);
}

void* FindSystemLibsFunction(const ModuleImage& image, const char* functionName) {
    if (void* fromOpenOs = FindSystemLibsFunctionFromOpenOs(image, functionName)) {
        return fromOpenOs;
    }

    if (void* classicMatch = FindClassicLuaRegFunction(image, functionName)) {
        return classicMatch;
    }

    return FindCitizenLuaTableFunction(image, functionName);
}

void* FindSiblingSandboxFunction(
    const ModuleImage& image,
    void* knownFunction,
    const char* knownName,
    const char* targetName) {
    if (knownFunction == nullptr || knownName == nullptr || targetName == nullptr) {
        return nullptr;
    }

    const char* knownNameString = FindNullTerminatedString(image, knownName);
    const char* targetNameString = FindNullTerminatedString(image, targetName);
    if (knownNameString == nullptr || targetNameString == nullptr) {
        return nullptr;
    }

    if (void* citizenSibling =
            FindCitizenLuaTableSiblingFunction(image, knownFunction, knownName, targetName)) {
        return citizenSibling;
    }

    const std::uintptr_t knownNamePointer = reinterpret_cast<std::uintptr_t>(knownNameString);
    const std::uintptr_t knownFunctionPointer = reinterpret_cast<std::uintptr_t>(knownFunction);

    auto scanSegment = [&](const std::uint8_t* base, std::size_t size) -> void* {
        if (base == nullptr || size < (2 * sizeof(std::uintptr_t))) {
            return nullptr;
        }

        for (std::size_t offset = 0; offset + (2 * sizeof(std::uintptr_t)) <= size;
             offset += sizeof(std::uintptr_t)) {
            const auto* entry = reinterpret_cast<const std::uintptr_t*>(base + offset);
            if (entry[0] != knownNamePointer || entry[1] != knownFunctionPointer) {
                continue;
            }

            for (int index = 0; index < 64; ++index) {
                const char* name = reinterpret_cast<const char*>(entry[index * 2]);
                if (name == nullptr || !IsAddressInsideModule(image, name)) {
                    break;
                }

                void* function = reinterpret_cast<void*>(entry[index * 2 + 1]);
                if (function == nullptr) {
                    break;
                }

                if (std::strcmp(name, targetName) == 0) {
                    if (!IsAddressExecutable(image, function)) {
                        return nullptr;
                    }

                    return function;
                }
            }
        }

        return nullptr;
    };

    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (void* found = scanSegment(segment.base, segment.size)) {
                return found;
            }
        }

        return nullptr;
    }

    if (image.base == nullptr || image.size == 0) {
        return nullptr;
    }

    return scanSegment(image.base, image.size);
}

void* FindSandboxExecuteFunction(const ModuleImage& image) {
    if (void* tableExecute = FindSystemLibsFunctionFromOpenOs(image, "execute")) {
        CitizenLuaApi api{};
        if (ResolveCitizenLuaApi(image, tableExecute, api)) {
            return tableExecute;
        }
    }

    if (void* tableExecute = FindCitizenLuaTableFunction(image, "execute")) {
        CitizenLuaApi api{};
        if (ResolveCitizenLuaApi(image, tableExecute, api)) {
            return tableExecute;
        }
    }

    const char* permissionDenied = FindNullTerminatedString(image, "Permission denied");
    if (permissionDenied == nullptr) {
        return nullptr;
    }

    void* hits[32]{};
    std::size_t hitCount = 0;
    if (!FindLeaRipReference(image, permissionDenied, 32, hits, hitCount)) {
        return nullptr;
    }

    for (std::size_t index = 0; index < hitCount; ++index) {
        void* candidate = FindFunctionStartFromInstruction(image, hits[index]);
        if (candidate == nullptr || !IsAddressExecutable(image, candidate)) {
            continue;
        }

        CitizenLuaApi api{};
        if (ResolveCitizenLuaApi(image, candidate, api)) {
            return candidate;
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
