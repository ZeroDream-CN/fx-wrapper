#pragma once

#include <cstdint>
#include <string>
#include <vector>

using ModuleHandle = void*;

struct ModuleSegment {
    const std::uint8_t* base = nullptr;
    std::size_t size = 0;
    bool executable = false;
};

struct ModuleImage {
    ModuleHandle handle = nullptr;
    const std::uint8_t* base = nullptr;
    std::size_t size = 0;
    std::vector<ModuleSegment> segments;
};

const char* FxServerExecutableName();
const char* HookLibraryFileName();
const char* ScriptingCoreModuleName();
const char* ScriptingLuaModuleName();
const char* ScriptingNodeModuleName();
const char* CoreRuntimeModuleName();

std::string GetExecutableDirectory();
std::string JoinPath(const std::string& directory, const std::string& fileName);
std::string GetPathFileName(const std::string& path);

void PlatformSleepMs(std::uint32_t milliseconds);
void DebugLog(const char* message);
void DebugLogLine(const char* message);

ModuleHandle GetModuleHandleByName(const char* moduleName);
ModuleHandle LoadServerModule(const char* moduleName);
bool IsModuleMapped(const char* moduleNameFragment);
bool GetModuleImageByFragment(const char* moduleNameFragment, ModuleImage& outImage);
void* GetModuleSymbol(ModuleHandle module, const char* symbolName);
void* GetModuleSymbol(ModuleHandle module, const std::vector<const char*>& symbolNames);
bool GetModuleImage(ModuleHandle module, ModuleImage& outImage);
bool IsAddressInsideModule(const ModuleImage& image, const void* address);
bool IsAddressExecutable(const ModuleImage& image, const void* address);

bool EnsureHookEngineInitialized();
bool CreateAndEnableHook(void* target, void* detour, void** original, const char* symbolName);
