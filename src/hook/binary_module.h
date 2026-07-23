#pragma once

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

const char* FindNullTerminatedString(const ModuleImage& image, const char* needle);
const char* FindCStringContainingFragment(const ModuleImage& image, const char* fragment);
void* FindFunctionFromStringAddress(const ModuleImage& image, const void* stringAddress);
void* FindFunctionContainingCStringFragment(const ModuleImage& image, const char* fragment);
bool TryDecodeRipRelativeReference(
    const std::uint8_t* bytes,
    std::size_t size,
    std::uintptr_t instructionAddress,
    std::uintptr_t target,
    std::uintptr_t& outReferencedAddress);
bool TryDecodeRipRelativeLea(
    const std::uint8_t* bytes,
    std::size_t size,
    std::uintptr_t instructionAddress,
    std::uintptr_t& outReferencedAddress);
bool FindLeaRipReference(
    const ModuleImage& image,
    const void* targetAddress,
    std::size_t maxHits,
    void* outHits[],
    std::size_t& outHitCount);
void* FindFunctionStartFromInstruction(const ModuleImage& image, const void* instructionAddress);
void* FindFunctionFromStringMarker(const ModuleImage& image, const char* markerString);
void* FindFunctionContainingStringMarker(const ModuleImage& image, const char* markerString);
void* FindFunctionContainingBothStringMarkers(
    const ModuleImage& image,
    const char* firstMarker,
    const char* secondMarker);
bool FunctionReferencesString(
    const ModuleImage& image,
    const void* functionAddress,
    const char* markerString,
    std::size_t scanSize = 0x3000);
