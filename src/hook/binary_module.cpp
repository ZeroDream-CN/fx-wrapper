#include "binary_module.h"

#include <algorithm>

namespace {

template <typename Callback>
void ForEachReadableSegment(const ModuleImage& image, Callback&& callback) {
    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (segment.base != nullptr && segment.size > 0) {
                callback(segment.base, segment.size);
            }
        }
        return;
    }

    if (image.base != nullptr && image.size > 0) {
        callback(image.base, image.size);
    }
}

int PrologueStrength(const std::uint8_t* candidate) {
    if (candidate[0] == 0x48U && candidate[1] == 0x89U && candidate[2] == 0x5CU && candidate[3] == 0x24U &&
        candidate[4] == 0x10U && candidate[5] == 0x55U) {
        return 100;
    }

    if (candidate[0] == 0x55U && candidate[1] == 0x48U && candidate[2] == 0x89U && candidate[3] == 0xE5U) {
        return 90;
    }

    if (candidate[0] == 0xF3U && candidate[1] == 0x0FU && candidate[2] == 0x1EU && candidate[3] == 0xFAU) {
        return 80;
    }

    if (candidate[0] == 0x40U && candidate[1] == 0x53U) {
        return 50;
    }

    if (candidate[0] == 0x53U) {
        return 50;
    }

    if (candidate[0] == 0x41U && candidate[1] >= 0x54U && candidate[1] <= 0x57U) {
        return 50;
    }

    if (candidate[0] == 0x55U) {
        return 10;
    }

    return 0;
}

bool LooksLikeFunctionPrologue(const std::uint8_t* candidate) {
    return PrologueStrength(candidate) > 0;
}

}  // namespace

bool TryDecodeRipRelativeReference(
    const std::uint8_t* bytes,
    const std::size_t size,
    const std::uintptr_t instructionAddress,
    const std::uintptr_t target,
    std::uintptr_t& outReferencedAddress) {
    if (TryDecodeRipRelativeLea(bytes, size, instructionAddress, outReferencedAddress) &&
        outReferencedAddress == target) {
        return true;
    }

    if (size < 7) {
        return false;
    }

    const bool isMov = bytes[1] == 0x8BU && (bytes[0] == 0x48U || bytes[0] == 0x4CU);
    if (!isMov) {
        return false;
    }

    if (!((bytes[2] & 0x07U) == 0x05U && (bytes[2] & 0xC0U) == 0x00U)) {
        return false;
    }

    const std::int32_t displacement = *reinterpret_cast<const std::int32_t*>(bytes + 3);
    outReferencedAddress = instructionAddress + 7U + static_cast<std::uintptr_t>(displacement);
    return outReferencedAddress == target;
}

bool TryDecodeRipRelativeLea(
    const std::uint8_t* bytes,
    const std::size_t size,
    const std::uintptr_t instructionAddress,
    std::uintptr_t& outReferencedAddress) {
    if (size < 7) {
        return false;
    }

    const bool isLea = bytes[1] == 0x8DU && (bytes[0] == 0x48U || bytes[0] == 0x4CU);
    if (!isLea) {
        return false;
    }

    if (!((bytes[2] & 0x07U) == 0x05U && (bytes[2] & 0xC0U) == 0x00U)) {
        return false;
    }

    const std::int32_t displacement = *reinterpret_cast<const std::int32_t*>(bytes + 3);
    outReferencedAddress = instructionAddress + 7U + static_cast<std::uintptr_t>(displacement);
    return true;
}

const char* FindNullTerminatedString(const ModuleImage& image, const char* needle) {
    if (needle == nullptr) {
        return nullptr;
    }

    const std::size_t needleLength = std::strlen(needle);
    if (needleLength == 0) {
        return nullptr;
    }

    const char* found = nullptr;
    ForEachReadableSegment(image, [&](const std::uint8_t* base, std::size_t size) {
        if (found != nullptr || needleLength + 1 > size) {
            return;
        }

        for (std::size_t offset = 0; offset + needleLength + 1 <= size; ++offset) {
            if (std::memcmp(base + offset, needle, needleLength) != 0) {
                continue;
            }

            if (base[offset + needleLength] != '\0') {
                continue;
            }

            found = reinterpret_cast<const char*>(base + offset);
            return;
        }
    });

    return found;
}

bool FindLeaRipReference(
    const ModuleImage& image,
    const void* targetAddress,
    const std::size_t maxHits,
    void* outHits[],
    std::size_t& outHitCount) {
    outHitCount = 0;
    if (targetAddress == nullptr || maxHits == 0) {
        return false;
    }

    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(targetAddress);
    bool foundAny = false;

    ForEachReadableSegment(image, [&](const std::uint8_t* base, std::size_t size) {
        if (outHitCount >= maxHits || size < 8) {
            return;
        }

        const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(base);
        for (std::size_t index = 0; index + 7 < size; ++index) {
            const std::uint8_t* bytes = base + index;
            const std::uintptr_t instructionAddress = imageBase + index;
            std::uintptr_t referencedAddress = 0;
            if (!TryDecodeRipRelativeReference(bytes, size - index, instructionAddress, target, referencedAddress)) {
                continue;
            }

            if (outHitCount < maxHits) {
                outHits[outHitCount++] = reinterpret_cast<void*>(instructionAddress);
                foundAny = true;
            }
        }
    });

    return foundAny;
}

void* FindFunctionStartFromInstruction(const ModuleImage& image, const void* instructionAddress) {
    if (instructionAddress == nullptr) {
        return nullptr;
    }

    const std::uintptr_t instruction = reinterpret_cast<std::uintptr_t>(instructionAddress);
    void* found = nullptr;

    ForEachReadableSegment(image, [&](const std::uint8_t* base, std::size_t size) {
        if (found != nullptr) {
            return;
        }

        const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(base);
        if (instruction < imageBase || instruction >= imageBase + size) {
            return;
        }

        const std::size_t instructionOffset = static_cast<std::size_t>(instruction - imageBase);
        void* bestMatch = nullptr;
        int bestStrength = 0;
        for (std::size_t back = 0; back < 0x800; ++back) {
            if (instructionOffset < back) {
                break;
            }

            const std::size_t candidateOffset = instructionOffset - back;
            const std::uint8_t* candidate = base + candidateOffset;
            const int strength = PrologueStrength(candidate);
            if (strength == 0) {
                continue;
            }

            void* function = reinterpret_cast<void*>(imageBase + candidateOffset);
            if (!IsAddressExecutable(image, function)) {
                continue;
            }

            if (strength >= 100) {
                found = function;
                return;
            }

            if (strength > bestStrength) {
                bestMatch = function;
                bestStrength = strength;
            }
        }

        found = bestMatch;
    });

    return found;
}

void* FindFunctionFromStringMarker(const ModuleImage& image, const char* markerString) {
    const char* marker = FindNullTerminatedString(image, markerString);
    if (marker == nullptr) {
        return nullptr;
    }

    void* hits[32]{};
    std::size_t hitCount = 0;
    if (!FindLeaRipReference(image, marker, 32, hits, hitCount) || hitCount == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < hitCount; ++index) {
        void* function = FindFunctionStartFromInstruction(image, hits[index]);
        if (function != nullptr && IsAddressExecutable(image, function)) {
            return function;
        }
    }

    return nullptr;
}

bool FunctionReferencesString(
    const ModuleImage& image,
    const void* functionAddress,
    const char* markerString,
    const std::size_t scanSize) {
    if (functionAddress == nullptr || markerString == nullptr) {
        return false;
    }

    const char* marker = FindNullTerminatedString(image, markerString);
    if (marker == nullptr) {
        return false;
    }

    const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(marker);
    const std::uintptr_t functionBegin = reinterpret_cast<std::uintptr_t>(functionAddress);

    auto scanRange = [&](const std::uint8_t* base, std::size_t size, std::uintptr_t rangeStart) -> bool {
        if (base == nullptr || size == 0 || rangeStart < reinterpret_cast<std::uintptr_t>(base)) {
            return false;
        }

        const std::size_t offset = static_cast<std::size_t>(rangeStart - reinterpret_cast<std::uintptr_t>(base));
        const std::size_t bytesToScan = std::min(scanSize, size - offset);
        for (std::size_t index = 0; index + 7 < bytesToScan; ++index) {
            std::uintptr_t referencedAddress = 0;
            if (!TryDecodeRipRelativeReference(
                    base + offset + index,
                    bytesToScan - index,
                    rangeStart + index,
                    target,
                    referencedAddress)) {
                continue;
            }

            return true;
        }

        return false;
    };

    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (!segment.executable || segment.base == nullptr || segment.size == 0) {
                continue;
            }

            const auto begin = reinterpret_cast<std::uintptr_t>(segment.base);
            const auto end = begin + segment.size;
            if (functionBegin < begin || functionBegin >= end) {
                continue;
            }

            if (scanRange(segment.base, segment.size, functionBegin)) {
                return true;
            }
        }

        return false;
    }

    if (image.base == nullptr || image.size == 0 || functionBegin < reinterpret_cast<std::uintptr_t>(image.base)) {
        return false;
    }

    return scanRange(image.base, image.size, functionBegin);
}

void* FindFunctionContainingStringMarker(const ModuleImage& image, const char* markerString) {
    return FindFunctionFromStringMarker(image, markerString);
}

void* FindFunctionContainingBothStringMarkers(
    const ModuleImage& image,
    const char* firstMarker,
    const char* secondMarker) {
    const char* first = FindNullTerminatedString(image, firstMarker);
    if (first == nullptr) {
        return nullptr;
    }

    void* hits[32]{};
    std::size_t hitCount = 0;
    if (!FindLeaRipReference(image, first, 32, hits, hitCount)) {
        return nullptr;
    }

    for (std::size_t index = 0; index < hitCount; ++index) {
        void* function = FindFunctionStartFromInstruction(image, hits[index]);
        if (function == nullptr || !IsAddressExecutable(image, function)) {
            continue;
        }

        if (FunctionReferencesString(image, function, secondMarker)) {
            return function;
        }
    }

    return nullptr;
}

const char* FindCStringContainingFragment(const ModuleImage& image, const char* fragment) {
    if (fragment == nullptr) {
        return nullptr;
    }

    const std::size_t fragmentLength = std::strlen(fragment);
    if (fragmentLength == 0) {
        return nullptr;
    }

    const char* found = nullptr;
    ForEachReadableSegment(image, [&](const std::uint8_t* base, std::size_t size) {
        if (found != nullptr || fragmentLength > size) {
            return;
        }

        for (std::size_t offset = 0; offset + fragmentLength <= size; ++offset) {
            if (std::memcmp(base + offset, fragment, fragmentLength) != 0) {
                continue;
            }

            std::size_t stringStart = offset;
            while (stringStart > 0 && base[stringStart - 1] != '\0') {
                --stringStart;
            }

            std::size_t stringEnd = offset + fragmentLength;
            while (stringEnd < size && base[stringEnd] != '\0') {
                ++stringEnd;
            }

            if (stringEnd >= size) {
                continue;
            }

            found = reinterpret_cast<const char*>(base + stringStart);
            return;
        }
    });

    return found;
}

void* FindFunctionFromStringAddress(const ModuleImage& image, const void* stringAddress) {
    if (stringAddress == nullptr) {
        return nullptr;
    }

    void* hits[32]{};
    std::size_t hitCount = 0;
    if (!FindLeaRipReference(image, stringAddress, 32, hits, hitCount) || hitCount == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < hitCount; ++index) {
        void* function = FindFunctionStartFromInstruction(image, hits[index]);
        if (function != nullptr && IsAddressExecutable(image, function)) {
            return function;
        }
    }

    return nullptr;
}

void* FindFunctionContainingCStringFragment(const ModuleImage& image, const char* fragment) {
    const char* stringAddress = FindCStringContainingFragment(image, fragment);
    if (stringAddress == nullptr) {
        return nullptr;
    }

    return FindFunctionFromStringAddress(image, stringAddress);
}
