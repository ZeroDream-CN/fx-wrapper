#include "platform/platform.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace {

std::string NarrowFromWide(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

}  // namespace

const char* FxServerExecutableName() {
    return "FXServer.exe";
}

const char* HookLibraryFileName() {
    return "fx-hook.dll";
}

const char* ScriptingCoreModuleName() {
    return "citizen-scripting-core.dll";
}

const char* ScriptingLuaModuleName() {
    return "citizen-scripting-lua.dll";
}

const char* ScriptingNodeModuleName() {
    return "citizen-scripting-node.dll";
}

const char* CoreRuntimeModuleName() {
    return "CoreRT.dll";
}

std::string GetExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }

        if (length < path.size()) {
            path.resize(length);
            return NarrowFromWide(std::filesystem::path(path).parent_path().wstring());
        }

        path.resize(path.size() * 2);
    }
}

std::string JoinPath(const std::string& directory, const std::string& fileName) {
    return (std::filesystem::path(directory) / fileName).string();
}

std::string GetPathFileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

void PlatformSleepMs(const std::uint32_t milliseconds) {
    Sleep(milliseconds);
}

void DebugLog(const char* message) {
    if (message != nullptr) {
        OutputDebugStringA(message);
    }
}

void DebugLogLine(const char* message) {
    if (message == nullptr) {
        return;
    }

    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

ModuleHandle GetModuleHandleByName(const char* moduleName) {
    return GetModuleHandleA(moduleName);
}

ModuleHandle LoadServerModule(const char* moduleName) {
    if (moduleName == nullptr || moduleName[0] == '\0') {
        return nullptr;
    }

    ModuleHandle existing = GetModuleHandleByName(moduleName);
    if (existing != nullptr) {
        return existing;
    }

    const std::string serverDirectory = GetExecutableDirectory();
    if (serverDirectory.empty()) {
        return nullptr;
    }

    const std::string modulePath = JoinPath(serverDirectory, moduleName);
    if (!std::filesystem::exists(modulePath)) {
        return nullptr;
    }

    return LoadLibraryA(modulePath.c_str());
}

bool IsModuleMapped(const char* moduleNameFragment) {
    if (moduleNameFragment == nullptr || moduleNameFragment[0] == '\0') {
        return false;
    }

    return GetModuleHandleByName(moduleNameFragment) != nullptr;
}

bool GetModuleImageByFragment(const char* moduleNameFragment, ModuleImage& outImage) {
    outImage = {};
    if (moduleNameFragment == nullptr || moduleNameFragment[0] == '\0') {
        return false;
    }

    ModuleHandle module = GetModuleHandleByName(moduleNameFragment);
    if (module != nullptr) {
        return GetModuleImage(module, outImage);
    }

    return false;
}

void* GetModuleSymbol(const ModuleHandle module, const char* symbolName) {
    if (module == nullptr || symbolName == nullptr) {
        return nullptr;
    }

    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), symbolName));
}

void* GetModuleSymbol(const ModuleHandle module, const std::vector<const char*>& symbolNames) {
    for (const char* symbolName : symbolNames) {
        void* symbol = GetModuleSymbol(module, symbolName);
        if (symbol != nullptr) {
            return symbol;
        }
    }

    return nullptr;
}

bool GetModuleImage(const ModuleHandle module, ModuleImage& outImage) {
    outImage = {};

    if (module == nullptr) {
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

    outImage.handle = module;
    outImage.base = reinterpret_cast<const std::uint8_t*>(module);
    outImage.size = ntHeaders->OptionalHeader.SizeOfImage;
    return true;
}

bool IsAddressInsideModule(const ModuleImage& image, const void* address) {
    if (image.base == nullptr || image.size == 0 || address == nullptr) {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(image.base);
    const auto end = begin + image.size;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= begin && value < end;
}

bool IsAddressExecutable(const ModuleImage& image, const void* address) {
    return IsAddressInsideModule(image, address);
}
