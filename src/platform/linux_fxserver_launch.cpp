#include "linux_fxserver_launch.h"

#include "platform/platform.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace {

std::vector<std::string> SplitNonEmptyColonPaths(const std::string& value) {
    std::vector<std::string> paths;
    std::string current;

    for (char ch : value) {
        if (ch == ':') {
            if (!current.empty()) {
                paths.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        paths.push_back(current);
    }

    return paths;
}

std::string JoinColonPaths(const std::vector<std::string>& paths) {
    std::string joined;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index > 0) {
            joined.push_back(':');
        }
        joined += paths[index];
    }

    return joined;
}

std::string RemoveColonPaths(const std::string& list, const std::string& pathsToRemove) {
    std::unordered_set<std::string> removeSet;
    for (const std::string& path : SplitNonEmptyColonPaths(pathsToRemove)) {
        removeSet.insert(path);
    }

    std::vector<std::string> kept;
    kept.reserve(8);
    for (const std::string& path : SplitNonEmptyColonPaths(list)) {
        if (removeSet.count(path) == 0) {
            kept.push_back(path);
        }
    }

    return JoinColonPaths(kept);
}

std::string MergeEnvPathList(const std::string& preferredPrefix, const std::string& existingValue) {
    if (preferredPrefix.empty()) {
        return existingValue;
    }

    std::vector<std::string> merged;
    std::unordered_set<std::string> seen;
    merged.reserve(8);

    auto appendUnique = [&](const std::string& path) {
        if (path.empty() || seen.count(path) > 0) {
            return;
        }

        seen.insert(path);
        merged.push_back(path);
    };

    for (const std::string& path : SplitNonEmptyColonPaths(preferredPrefix)) {
        appendUnique(path);
    }

    for (const std::string& path : SplitNonEmptyColonPaths(existingValue)) {
        appendUnique(path);
    }

    return JoinColonPaths(merged);
}

}  // namespace

bool PathExistsOnDisk(const std::string& path) {
    return std::filesystem::exists(path);
}

bool IsFxServerBinaryPath(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    return GetPathFileName(path) == FxServerExecutableName();
}

bool IsMuslLoaderPath(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    return GetPathFileName(path) == "ld-musl-x86_64.so.1";
}

std::string ResolveFxServerPathForLaunch(const char* path, char* const argv[]) {
    const auto tryCandidate = [](const std::string& candidate) -> std::string {
        if (candidate.empty() || GetPathFileName(candidate) != FxServerExecutableName()) {
            return {};
        }

        if (candidate.find('/') != std::string::npos && PathExistsOnDisk(candidate)) {
            return candidate;
        }

        const std::string besideExecutable = JoinPath(GetExecutableDirectory(), FxServerExecutableName());
        if (PathExistsOnDisk(besideExecutable)) {
            return besideExecutable;
        }

        if (candidate.find('/') != std::string::npos) {
            return candidate;
        }

        return besideExecutable;
    };

    if (path != nullptr) {
        if (const std::string resolved = tryCandidate(path); !resolved.empty()) {
            return resolved;
        }
    }

    if (argv != nullptr) {
        for (char* const* cursor = argv; *cursor != nullptr; ++cursor) {
            if (const std::string resolved = tryCandidate(*cursor); !resolved.empty()) {
                return resolved;
            }
        }
    }

    return {};
}

std::string GetDirectoryName(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::string DetectAlpineRootFromServerDir(const std::string& cfxServerDir) {
    const std::vector<std::string> candidates = {
        JoinPath(cfxServerDir, "../.."),
        JoinPath(cfxServerDir, "../../.."),
        JoinPath(cfxServerDir, ".."),
    };

    for (const std::string& candidatePath : candidates) {
        std::error_code errorCode;
        const std::filesystem::path candidate = std::filesystem::weakly_canonical(candidatePath, errorCode);
        if (errorCode) {
            continue;
        }

        if (PathExistsOnDisk((candidate / "usr/lib").string())) {
            return candidate.string();
        }
    }

    return {};
}

std::string BuildAlpineLibraryPath(const std::string& alpineRoot) {
    return MergeEnvPathList(
        JoinPath(alpineRoot, "usr/lib/v8") + ":" + JoinPath(alpineRoot, "lib") + ":" + JoinPath(alpineRoot, "usr/lib"),
        std::string{});
}

std::string MergeColonSeparatedPaths(const std::string& preferredPrefix, const std::string& existingValue) {
    return MergeEnvPathList(preferredPrefix, existingValue);
}

MuslLaunchPlan BuildMuslLaunchPlan(const char* fxServerPath, char* const argv[]) {
    MuslLaunchPlan plan{};
    if (fxServerPath == nullptr || fxServerPath[0] == '\0') {
        return plan;
    }

    const std::string fxServerDir = GetDirectoryName(fxServerPath);
    const std::string muslLoaderPath = JoinPath(fxServerDir, "ld-musl-x86_64.so.1");
    const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
    const std::string libraryPath = alpineRoot.empty() ? std::string{} : BuildAlpineLibraryPath(alpineRoot);

    auto buildDirectPlan = [&]() {
        plan.execPath = fxServerPath;
        if (argv != nullptr) {
            for (char* const* cursor = argv; *cursor != nullptr; ++cursor) {
                plan.argStorage.emplace_back(*cursor);
            }
        } else {
            plan.argStorage.emplace_back(fxServerPath);
        }

        for (std::string& argument : plan.argStorage) {
            plan.argv.push_back(argument.data());
        }
        plan.argv.push_back(nullptr);
    };

    if (!PathExistsOnDisk(muslLoaderPath) || libraryPath.empty()) {
        buildDirectPlan();
        return plan;
    }

    const std::string citizenDir = JoinPath(fxServerDir, "citizen");

    plan.execPath = muslLoaderPath;
    plan.argStorage = {
        muslLoaderPath,
        "--library-path",
        libraryPath,
        "--",
        fxServerPath,
        "+set",
        "citizen_dir",
        citizenDir,
    };

    if (argv != nullptr) {
        for (char* const* cursor = argv + 1; *cursor != nullptr; ++cursor) {
            plan.argStorage.emplace_back(*cursor);
        }
    }

    for (std::string& argument : plan.argStorage) {
        plan.argv.push_back(argument.data());
    }
    plan.argv.push_back(nullptr);
    return plan;
}

MuslLaunchPlan BuildLaunchPlanForFxServerSpawn(
    const char* pathname,
    char* const argv[],
    const std::string& fxServerPath) {
    if (IsMuslLoaderPath(pathname)) {
        MuslLaunchPlan plan{};
        if (pathname != nullptr) {
            plan.execPath = pathname;
        }

        if (argv != nullptr) {
            for (char* const* cursor = argv; *cursor != nullptr; ++cursor) {
                plan.argStorage.emplace_back(*cursor);
            }
        }

        for (std::string& argument : plan.argStorage) {
            plan.argv.push_back(argument.data());
        }
        plan.argv.push_back(nullptr);
        return plan;
    }

    return BuildMuslLaunchPlan(fxServerPath.c_str(), argv);
}

char* const* BuildFxServerEnviron(
    char* const envp[],
    const char* hookLibraryPath,
    const std::string& alpineLibraryPath) {
    static thread_local std::vector<std::string> storage;
    static thread_local std::vector<char*> pointers;
    storage.clear();
    pointers.clear();

    constexpr const char* kPreloadPrefix = "LD_PRELOAD=";
    constexpr const char* kLibraryPathPrefix = "LD_LIBRARY_PATH=";
    bool replacedPreload = false;
    bool replacedLibraryPath = false;

    if (envp != nullptr) {
        for (char* const* cursor = envp; *cursor != nullptr; ++cursor) {
            const std::string entry = *cursor;
            if (entry.rfind(kPreloadPrefix, 0) == 0) {
                if (hookLibraryPath != nullptr && hookLibraryPath[0] != '\0') {
                    const std::string existing = entry.substr(std::strlen(kPreloadPrefix));
                    storage.push_back(kPreloadPrefix + MergeEnvPathList(hookLibraryPath, existing));
                } else {
                    storage.push_back(entry);
                }
                replacedPreload = true;
                continue;
            }

            if (!alpineLibraryPath.empty() && entry.rfind(kLibraryPathPrefix, 0) == 0) {
                const std::string existing = entry.substr(std::strlen(kLibraryPathPrefix));
                storage.push_back(kLibraryPathPrefix + MergeEnvPathList(alpineLibraryPath, existing));
                replacedLibraryPath = true;
                continue;
            }

            storage.push_back(entry);
        }
    }

    if (!replacedPreload && hookLibraryPath != nullptr && hookLibraryPath[0] != '\0') {
        storage.push_back(std::string(kPreloadPrefix) + hookLibraryPath);
    }

    if (!replacedLibraryPath && !alpineLibraryPath.empty()) {
        storage.push_back(std::string(kLibraryPathPrefix) + alpineLibraryPath);
    }

    for (std::string& entry : storage) {
        pointers.push_back(entry.data());
    }
    pointers.push_back(nullptr);
    return pointers.data();
}

std::string RemoveHookLibraryFromPreloadList(const std::string& preloadValue) {
    const std::string hookLibraryName = HookLibraryFileName();
    std::vector<std::string> kept;
    kept.reserve(4);

    for (const std::string& path : SplitNonEmptyColonPaths(preloadValue)) {
        if (GetPathFileName(path) == hookLibraryName) {
            continue;
        }

        kept.push_back(path);
    }

    return JoinColonPaths(kept);
}

std::string RemoveAlpineBootstrapPathsFromLibraryPath(const std::string& libraryPathValue) {
    const std::string fxServerDir = GetExecutableDirectory();
    const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
    if (alpineRoot.empty()) {
        return libraryPathValue;
    }

    return RemoveColonPaths(libraryPathValue, BuildAlpineLibraryPath(alpineRoot));
}

void DropFxHookEnvironmentFromProcess() {
    if (const char* preload = std::getenv("LD_PRELOAD"); preload != nullptr && preload[0] != '\0') {
        const std::string cleaned = RemoveHookLibraryFromPreloadList(preload);
        if (cleaned.empty()) {
            unsetenv("LD_PRELOAD");
        } else {
            setenv("LD_PRELOAD", cleaned.c_str(), 1);
        }
    }

    if (const char* libraryPath = std::getenv("LD_LIBRARY_PATH");
        libraryPath != nullptr && libraryPath[0] != '\0') {
        const std::string cleaned = RemoveAlpineBootstrapPathsFromLibraryPath(libraryPath);
        if (cleaned.empty()) {
            unsetenv("LD_LIBRARY_PATH");
        } else {
            setenv("LD_LIBRARY_PATH", cleaned.c_str(), 1);
        }
    }
}

char* const* SanitizeEnvironmentForHostBinary(char* const envp[]) {
    static thread_local std::vector<std::string> storage;
    static thread_local std::vector<char*> pointers;
    storage.clear();
    pointers.clear();

    constexpr const char* kPreloadPrefix = "LD_PRELOAD=";
    constexpr const char* kLibraryPathPrefix = "LD_LIBRARY_PATH=";

    auto appendSanitizedEntries = [&](char* const* source) {
        if (source == nullptr) {
            return;
        }

        for (char* const* cursor = source; *cursor != nullptr; ++cursor) {
            const std::string entry = *cursor;
            if (entry.rfind(kPreloadPrefix, 0) == 0) {
                const std::string cleaned =
                    RemoveHookLibraryFromPreloadList(entry.substr(std::strlen(kPreloadPrefix)));
                if (!cleaned.empty()) {
                    storage.push_back(std::string(kPreloadPrefix) + cleaned);
                }
                continue;
            }

            if (entry.rfind(kLibraryPathPrefix, 0) == 0) {
                const std::string cleaned = RemoveAlpineBootstrapPathsFromLibraryPath(
                    entry.substr(std::strlen(kLibraryPathPrefix)));
                if (!cleaned.empty()) {
                    storage.push_back(std::string(kLibraryPathPrefix) + cleaned);
                }
                continue;
            }

            storage.push_back(entry);
        }
    };

    appendSanitizedEntries(envp);
    if (envp == nullptr) {
        extern char** environ;
        appendSanitizedEntries(environ);
    }

    if (storage.empty()) {
        pointers.push_back(nullptr);
        return pointers.data();
    }

    for (std::string& entry : storage) {
        pointers.push_back(entry.data());
    }
    pointers.push_back(nullptr);
    return pointers.data();
}

int RunMuslShellCommand(const std::string& command) {
    if (command.empty()) {
        return 0;
    }

    const std::string fxServerDir = GetExecutableDirectory();
    const std::string muslLoaderPath = JoinPath(fxServerDir, "ld-musl-x86_64.so.1");
    const std::string alpineRoot = DetectAlpineRootFromServerDir(fxServerDir);
    if (!PathExistsOnDisk(muslLoaderPath) || alpineRoot.empty()) {
        return std::system(command.c_str());
    }

    const std::string libraryPath = BuildAlpineLibraryPath(alpineRoot);
    const std::string busyboxPath = JoinPath(alpineRoot, "bin/busybox");
    if (!PathExistsOnDisk(busyboxPath)) {
        return std::system(command.c_str());
    }

    std::vector<std::string> argStorage = {
        muslLoaderPath,
        "--library-path",
        libraryPath,
        "--",
        busyboxPath,
        "sh",
        "-c",
        command,
    };

    std::vector<char*> argv;
    argv.reserve(argStorage.size() + 1);
    for (std::string& argument : argStorage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == 0) {
        execv(muslLoaderPath.c_str(), argv.data());
        _exit(127);
    }

    if (pid < 0) {
        return -1;
    }

    int waitStatus = 0;
    if (waitpid(pid, &waitStatus, 0) < 0) {
        return -1;
    }

    return waitStatus;
}
