#include "platform/platform.h"

#include <dlfcn.h>
#include <link.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::string FindMappedObjectPath(const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return {};
    }

    std::string lastMatch;
    FILE* mapsFile = std::fopen("/proc/self/maps", "re");
    if (mapsFile == nullptr) {
        return {};
    }

    char line[1024];
    while (std::fgets(line, sizeof(line), mapsFile) != nullptr) {
        if (std::strstr(line, needle) == nullptr) {
            continue;
        }

        char* pathStart = std::strrchr(line, ' ');
        if (pathStart == nullptr || pathStart[1] == '\0' || pathStart[1] == '\n') {
            continue;
        }

        ++pathStart;
        lastMatch = pathStart;
        while (!lastMatch.empty() && (lastMatch.back() == '\n' || lastMatch.back() == '\r')) {
            lastMatch.pop_back();
        }
    }

    std::fclose(mapsFile);
    return lastMatch;
}

ModuleHandle TryOpenMappedModule(const char* moduleName) {
    ModuleHandle handle = dlopen(moduleName, RTLD_NOLOAD | RTLD_NOW);
    if (handle != nullptr) {
        return handle;
    }

    const std::string mappedPath = FindMappedObjectPath(moduleName);
    if (!mappedPath.empty()) {
        handle = dlopen(mappedPath.c_str(), RTLD_NOLOAD | RTLD_NOW);
        if (handle != nullptr) {
            return handle;
        }
    }

    const std::string serverDirectory = GetExecutableDirectory();
    if (!serverDirectory.empty()) {
        const std::string candidatePath = JoinPath(serverDirectory, moduleName);
        if (std::filesystem::exists(candidatePath)) {
            handle = dlopen(candidatePath.c_str(), RTLD_NOLOAD | RTLD_NOW);
            if (handle != nullptr) {
                return handle;
            }
        }
    }

    if (std::strncmp(moduleName, "lib", 3) == 0) {
        const std::string shortNeedle = moduleName + 3;
        const std::string shortMappedPath = FindMappedObjectPath(shortNeedle.c_str());
        if (!shortMappedPath.empty()) {
            handle = dlopen(shortMappedPath.c_str(), RTLD_NOLOAD | RTLD_NOW);
            if (handle != nullptr) {
                return handle;
            }
        }
    }

    return nullptr;
}

struct FindModuleByNameContext {
    std::string targetName;
    ModuleHandle handle = nullptr;
};

int FindLoadedModuleByName(struct dl_phdr_info* info, size_t /*size*/, void* data) {
    auto* context = static_cast<FindModuleByNameContext*>(data);
    if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
        return 0;
    }

    const std::string modulePath = info->dlpi_name;
    const std::string baseName = std::filesystem::path(modulePath).filename().string();
    if (baseName != context->targetName && modulePath.find(context->targetName) == std::string::npos) {
        return 0;
    }

    context->handle = dlopen(info->dlpi_name, RTLD_NOLOAD | RTLD_NOW);
    return context->handle != nullptr ? 1 : 0;
}

bool CollectMappedSegments(
    const char* mappedPath,
    const char* fragmentNeedle,
    std::vector<ModuleSegment>& segments) {
    segments.clear();
    if ((mappedPath == nullptr || mappedPath[0] == '\0') &&
        (fragmentNeedle == nullptr || fragmentNeedle[0] == '\0')) {
        return false;
    }

    FILE* mapsFile = std::fopen("/proc/self/maps", "re");
    if (mapsFile == nullptr) {
        return false;
    }

    char line[1024];
    while (std::fgets(line, sizeof(line), mapsFile) != nullptr) {
        const bool pathMatch = mappedPath != nullptr && mappedPath[0] != '\0' &&
            std::strstr(line, mappedPath) != nullptr;
        const bool fragmentMatch = fragmentNeedle != nullptr && fragmentNeedle[0] != '\0' &&
            std::strstr(line, fragmentNeedle) != nullptr;
        if (!pathMatch && !fragmentMatch) {
            continue;
        }

        std::uintptr_t regionStart = 0;
        std::uintptr_t regionEnd = 0;
        char permissions[8] = {};
        if (std::sscanf(line, "%lx-%lx %7s", &regionStart, &regionEnd, permissions) < 3) {
            continue;
        }

        if (permissions[0] != 'r' || regionEnd <= regionStart) {
            continue;
        }

        segments.push_back(
            {reinterpret_cast<const std::uint8_t*>(regionStart),
             static_cast<std::size_t>(regionEnd - regionStart),
             permissions[2] == 'x'});
    }

    std::fclose(mapsFile);
    return !segments.empty();
}

void FinalizeModuleImage(ModuleImage& image) {
    if (image.segments.empty()) {
        return;
    }

    const ModuleSegment* largest = &image.segments.front();
    for (const ModuleSegment& segment : image.segments) {
        if (segment.size > largest->size) {
            largest = &segment;
        }
    }

    image.base = largest->base;
    image.size = largest->size;
}

}  // namespace

const char* FxServerExecutableName() {
    return "FXServer";
}

const char* HookLibraryFileName() {
    return "libfx-hook.so";
}

const char* ScriptingCoreModuleName() {
    return "libcitizen-scripting-core.so";
}

const char* ScriptingLuaModuleName() {
    return "libcitizen-scripting-lua.so";
}

const char* ScriptingNodeModuleName() {
    return "libcitizen-scripting-node.so";
}

const char* CoreRuntimeModuleName() {
    return "libCoreRT.so";
}

std::string GetExecutableDirectory() {
    std::string path(PATH_MAX, '\0');
    while (true) {
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0) {
            return {};
        }

        if (static_cast<std::size_t>(length) < path.size()) {
            path.resize(static_cast<std::size_t>(length));
            return std::filesystem::path(path).parent_path().string();
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
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
}

bool IsFxHookDebugEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("FX_HOOK_DEBUG");
        if (value == nullptr || value[0] == '\0') {
            return false;
        }

        if (value[0] == '0' && value[1] == '\0') {
            return false;
        }

        if ((value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T') &&
            value[1] == '\0') {
            return true;
        }

        auto equalsIgnoreCase = [&](const char* literal) {
            for (std::size_t index = 0; literal[index] != '\0'; ++index) {
                char ch = value[index];
                if (ch == '\0') {
                    return false;
                }

                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }

                if (ch != literal[index]) {
                    return false;
                }
            }

            return value[std::strlen(literal)] == '\0';
        };

        return equalsIgnoreCase("true") || equalsIgnoreCase("yes") || equalsIgnoreCase("on");
    }();

    return enabled;
}

void DebugLog(const char* message) {
    if (!IsFxHookDebugEnabled() || message == nullptr) {
        return;
    }

    fputs(message, stderr);
}

void DebugLogLine(const char* message) {
    if (!IsFxHookDebugEnabled() || message == nullptr) {
        return;
    }

    fputs(message, stderr);
    fputc('\n', stderr);
}

ModuleHandle GetModuleHandleByName(const char* moduleName) {
    if (moduleName == nullptr) {
        return nullptr;
    }

    ModuleHandle handle = TryOpenMappedModule(moduleName);
    if (handle != nullptr) {
        return handle;
    }

    FindModuleByNameContext context;
    context.targetName = moduleName;
    dl_iterate_phdr(FindLoadedModuleByName, &context);
    if (context.handle != nullptr) {
        return context.handle;
    }

    return nullptr;
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

    dlerror();
    ModuleHandle handle = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        const char* firstError = dlerror();
        DebugLog("[fx-hook] dlopen failed for ");
        DebugLog(modulePath.c_str());
        DebugLog(": ");
        DebugLogLine(firstError != nullptr ? firstError : "unknown error");

        dlerror();
        handle = dlopen(modulePath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    }

    if (handle == nullptr) {
        const char* secondError = dlerror();
        DebugLog("[fx-hook] lazy dlopen failed for ");
        DebugLog(modulePath.c_str());
        DebugLog(": ");
        DebugLogLine(secondError != nullptr ? secondError : "unknown error");
        return nullptr;
    }

    return handle;
}

bool GetModuleImageByFragment(const char* moduleNameFragment, ModuleImage& outImage) {
    outImage = {};
    if (moduleNameFragment == nullptr || moduleNameFragment[0] == '\0') {
        return false;
    }

    const std::string mappedPath = FindMappedObjectPath(moduleNameFragment);
    if (mappedPath.empty()) {
        return false;
    }

    outImage.handle = dlopen(mappedPath.c_str(), RTLD_NOLOAD | RTLD_NOW);
    if (!CollectMappedSegments(mappedPath.c_str(), moduleNameFragment, outImage.segments)) {
        return false;
    }

    FinalizeModuleImage(outImage);
    return outImage.base != nullptr && outImage.size > 0;
}

bool IsModuleMapped(const char* moduleNameFragment) {
    if (moduleNameFragment == nullptr || moduleNameFragment[0] == '\0') {
        return false;
    }

    return !FindMappedObjectPath(moduleNameFragment).empty();
}

void* GetModuleSymbol(const ModuleHandle module, const char* symbolName) {
    if (symbolName == nullptr) {
        return nullptr;
    }

    if (module != nullptr) {
        dlerror();
        void* symbol = dlsym(module, symbolName);
        if (symbol != nullptr) {
            return symbol;
        }
    }

    dlerror();
    return dlsym(RTLD_DEFAULT, symbolName);
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

    Dl_info info{};
    if (dladdr(module, &info) == 0 || info.dli_fbase == nullptr) {
        return false;
    }

    outImage.handle = module;
    const std::string fileName = info.dli_fname != nullptr
        ? std::filesystem::path(info.dli_fname).filename().string()
        : std::string{};
    const std::string mappedPath =
        info.dli_fname != nullptr ? std::string(info.dli_fname) : FindMappedObjectPath(fileName.c_str());

    if (CollectMappedSegments(mappedPath.c_str(), fileName.c_str(), outImage.segments)) {
        FinalizeModuleImage(outImage);
        return outImage.base != nullptr && outImage.size > 0;
    }

    outImage.base = static_cast<const std::uint8_t*>(info.dli_fbase);
    outImage.segments.push_back({outImage.base, 0});
    return outImage.base != nullptr;
}

bool IsAddressInsideModule(const ModuleImage& image, const void* address) {
    if (address == nullptr) {
        return false;
    }

    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (segment.base == nullptr || segment.size == 0) {
                continue;
            }

            const auto begin = reinterpret_cast<std::uintptr_t>(segment.base);
            const auto end = begin + segment.size;
            if (value >= begin && value < end) {
                return true;
            }
        }

        return false;
    }

    if (image.base == nullptr || image.size == 0) {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(image.base);
    const auto end = begin + image.size;
    return value >= begin && value < end;
}

bool IsAddressExecutable(const ModuleImage& image, const void* address) {
    if (address == nullptr) {
        return false;
    }

    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (!image.segments.empty()) {
        for (const ModuleSegment& segment : image.segments) {
            if (!segment.executable || segment.base == nullptr || segment.size == 0) {
                continue;
            }

            const auto begin = reinterpret_cast<std::uintptr_t>(segment.base);
            const auto end = begin + segment.size;
            if (value >= begin && value < end) {
                return true;
            }
        }

        return false;
    }

    return IsAddressInsideModule(image, address);
}
