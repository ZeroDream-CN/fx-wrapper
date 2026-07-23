#include "hook_util.h"

#include "fx_log.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <string_view>
#include <tlhelp32.h>

#include <atomic>

namespace {

constexpr DWORD kChildInjectionWaitMs = 15000;

char g_hookLibraryPath[MAX_PATH]{};
std::atomic<bool> g_spawnHooksInstalled{false};

void CacheHookLibraryPath() {
    if (g_hookLibraryPath[0] != '\0') {
        return;
    }

    HMODULE selfModule = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&CacheHookLibraryPath),
            &selfModule)) {
        return;
    }

    const DWORD length = GetModuleFileNameA(selfModule, g_hookLibraryPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        g_hookLibraryPath[0] = '\0';
    }
}

std::string GetBaseName(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return {};
    }

    std::string normalized(path);
    if (!normalized.empty() && normalized.front() == '"' && normalized.back() == '"' && normalized.size() > 1) {
        normalized = normalized.substr(1, normalized.size() - 2);
    }

    const size_t slash = normalized.find_last_of("\\/");
    if (slash != std::string::npos) {
        return normalized.substr(slash + 1);
    }

    return normalized;
}

std::wstring GetBaseNameW(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return {};
    }

    std::wstring normalized(path);
    if (!normalized.empty() && normalized.front() == L'"' && normalized.back() == L'"' && normalized.size() > 1) {
        normalized = normalized.substr(1, normalized.size() - 2);
    }

    const size_t slash = normalized.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        return normalized.substr(slash + 1);
    }

    return normalized;
}

bool IsRemoteModuleLoaded(DWORD processId, const char* moduleName) {
    if (processId == 0 || moduleName == nullptr || moduleName[0] == '\0') {
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Module32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (Module32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool WaitForRemoteModuleLoaded(HANDLE processHandle, const char* moduleName, DWORD timeoutMs) {
    const DWORD processId = GetProcessId(processHandle);
    const ULONGLONG startTick = GetTickCount64();

    while (GetTickCount64() - startTick < timeoutMs) {
        if (IsRemoteModuleLoaded(processId, moduleName)) {
            return true;
        }

        PlatformSleepMs(50);
    }

    return IsRemoteModuleLoaded(processId, moduleName);
}

std::string ExtractFirstCommandLineToken(const char* commandLine) {
    if (commandLine == nullptr || commandLine[0] == '\0') {
        return {};
    }

    std::string_view view(commandLine);
    while (!view.empty() && view.front() == ' ') {
        view.remove_prefix(1);
    }

    if (view.empty()) {
        return {};
    }

    if (view.front() == '"') {
        view.remove_prefix(1);
        const size_t endQuote = view.find('"');
        if (endQuote == std::string_view::npos) {
            return std::string(view);
        }
        return std::string(view.substr(0, endQuote));
    }

    const size_t space = view.find(' ');
    if (space == std::string_view::npos) {
        return std::string(view);
    }

    return std::string(view.substr(0, space));
}

std::wstring ExtractFirstCommandLineTokenW(const wchar_t* commandLine) {
    if (commandLine == nullptr || commandLine[0] == L'\0') {
        return {};
    }

    std::wstring_view view(commandLine);
    while (!view.empty() && view.front() == L' ') {
        view.remove_prefix(1);
    }

    if (view.empty()) {
        return {};
    }

    if (view.front() == L'"') {
        view.remove_prefix(1);
        const size_t endQuote = view.find(L'"');
        if (endQuote == std::wstring_view::npos) {
            return std::wstring(view);
        }
        return std::wstring(view.substr(0, endQuote));
    }

    const size_t space = view.find(L' ');
    if (space == std::wstring_view::npos) {
        return std::wstring(view);
    }

    return std::wstring(view.substr(0, space));
}

bool IsFxServerLaunchA(LPCSTR applicationName, LPCSTR commandLine) {
    constexpr const char kFxServerExe[] = "FXServer.exe";

    if (applicationName != nullptr && applicationName[0] != '\0') {
        return _stricmp(GetBaseName(applicationName).c_str(), kFxServerExe) == 0;
    }

    const std::string firstToken = ExtractFirstCommandLineToken(commandLine);
    if (!firstToken.empty()) {
        return _stricmp(GetBaseName(firstToken.c_str()).c_str(), kFxServerExe) == 0;
    }

    if (commandLine != nullptr && commandLine[0] != '\0') {
        return strstr(commandLine, "FXServer.exe") != nullptr || strstr(commandLine, "fxserver.exe") != nullptr;
    }

    return false;
}

bool IsFxServerLaunchW(LPCWSTR applicationName, LPCWSTR commandLine) {
    constexpr const wchar_t kFxServerExe[] = L"FXServer.exe";

    if (applicationName != nullptr && applicationName[0] != L'\0') {
        return _wcsicmp(GetBaseNameW(applicationName).c_str(), kFxServerExe) == 0;
    }

    const std::wstring firstToken = ExtractFirstCommandLineTokenW(commandLine);
    if (!firstToken.empty()) {
        return _wcsicmp(GetBaseNameW(firstToken.c_str()).c_str(), kFxServerExe) == 0;
    }

    if (commandLine != nullptr && commandLine[0] != L'\0') {
        return wcsstr(commandLine, L"FXServer.exe") != nullptr || wcsstr(commandLine, L"fxserver.exe") != nullptr;
    }

    return false;
}

bool InjectHookLibraryRemoteThread(void* processHandle, const char* libraryPath) {
    if (processHandle == nullptr || libraryPath == nullptr || libraryPath[0] == '\0') {
        return false;
    }

    const size_t byteCount = std::strlen(libraryPath) + 1;
    void* remoteMemory = VirtualAllocEx(
        static_cast<HANDLE>(processHandle),
        nullptr,
        byteCount,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (remoteMemory == nullptr) {
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(
            static_cast<HANDLE>(processHandle),
            remoteMemory,
            libraryPath,
            byteCount,
            &bytesWritten) ||
        bytesWritten != byteCount) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryA"));
    if (loadLibraryA == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HANDLE remoteThread = CreateRemoteThread(
        static_cast<HANDLE>(processHandle),
        nullptr,
        0,
        loadLibraryA,
        remoteMemory,
        0,
        nullptr);
    if (remoteThread == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remoteThread, kChildInjectionWaitMs);
    CloseHandle(remoteThread);
    return true;
}

void PropagateHookToChildProcess(HANDLE processHandle, HANDLE threadHandle, bool alreadySuspended) {
    const char* hookLibraryPath = GetHookLibraryPath();
    if (hookLibraryPath == nullptr || hookLibraryPath[0] == '\0') {
        return;
    }

    const bool apcQueued = InjectHookLibraryApc(processHandle, threadHandle, hookLibraryPath);

    if (!alreadySuspended) {
        ResumeThread(threadHandle);

        if (apcQueued &&
            WaitForRemoteModuleLoaded(processHandle, HookLibraryFileName(), kChildInjectionWaitMs)) {
            DebugLogLine("[fx-hook] Propagated hook library to child FXServer.exe");
            return;
        }

        DebugLogLine("[fx-hook] APC injection did not load module, trying CreateRemoteThread fallback");
        if (InjectHookLibraryRemoteThread(processHandle, hookLibraryPath) &&
            WaitForRemoteModuleLoaded(processHandle, HookLibraryFileName(), kChildInjectionWaitMs)) {
            DebugLogLine("[fx-hook] Propagated hook library to child FXServer.exe (remote thread)");
            return;
        }

        DebugLogLine("[fx-hook] Failed to propagate hook library to child FXServer.exe");
        return;
    }

    if (apcQueued) {
        DebugLogLine("[fx-hook] Queued hook library injection for suspended child FXServer.exe");
    } else {
        DebugLogLine("[fx-hook] Failed to queue hook library injection for suspended child FXServer.exe");
    }
}

using CreateProcessAFn = BOOL(WINAPI*)(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation);

using CreateProcessWFn = BOOL(WINAPI*)(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation);

CreateProcessAFn g_originalCreateProcessA = nullptr;
CreateProcessWFn g_originalCreateProcessW = nullptr;

BOOL WINAPI HookedCreateProcessA(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {
    if (g_originalCreateProcessA == nullptr) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    if (lpProcessInformation == nullptr || !IsFxServerLaunchA(lpApplicationName, lpCommandLine)) {
        return g_originalCreateProcessA(
            lpApplicationName,
            lpCommandLine,
            lpProcessAttributes,
            lpThreadAttributes,
            bInheritHandles,
            dwCreationFlags,
            lpEnvironment,
            lpCurrentDirectory,
            lpStartupInfo,
            lpProcessInformation);
    }

    const bool alreadySuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
    const DWORD creationFlags = dwCreationFlags | CREATE_SUSPENDED;

    const BOOL created = g_originalCreateProcessA(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        creationFlags,
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation);

    if (!created) {
        return FALSE;
    }

    PropagateHookToChildProcess(
        lpProcessInformation->hProcess,
        lpProcessInformation->hThread,
        alreadySuspended);

    return TRUE;
}

BOOL WINAPI HookedCreateProcessW(
    LPCWSTR lpApplicationName,
    LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {
    if (g_originalCreateProcessW == nullptr) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    if (lpProcessInformation == nullptr || !IsFxServerLaunchW(lpApplicationName, lpCommandLine)) {
        return g_originalCreateProcessW(
            lpApplicationName,
            lpCommandLine,
            lpProcessAttributes,
            lpThreadAttributes,
            bInheritHandles,
            dwCreationFlags,
            lpEnvironment,
            lpCurrentDirectory,
            lpStartupInfo,
            lpProcessInformation);
    }

    const bool alreadySuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
    const DWORD creationFlags = dwCreationFlags | CREATE_SUSPENDED;

    const BOOL created = g_originalCreateProcessW(
        lpApplicationName,
        lpCommandLine,
        lpProcessAttributes,
        lpThreadAttributes,
        bInheritHandles,
        creationFlags,
        lpEnvironment,
        lpCurrentDirectory,
        lpStartupInfo,
        lpProcessInformation);

    if (!created) {
        return FALSE;
    }

    PropagateHookToChildProcess(
        lpProcessInformation->hProcess,
        lpProcessInformation->hThread,
        alreadySuspended);

    return TRUE;
}

}  // namespace

bool InjectHookLibraryApc(void* processHandle, void* mainThreadHandle, const char* libraryPath) {
    if (processHandle == nullptr || mainThreadHandle == nullptr || libraryPath == nullptr || libraryPath[0] == '\0') {
        return false;
    }

    const size_t byteCount = std::strlen(libraryPath) + 1;
    void* remoteMemory = VirtualAllocEx(
        static_cast<HANDLE>(processHandle),
        nullptr,
        byteCount,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (remoteMemory == nullptr) {
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(
            static_cast<HANDLE>(processHandle),
            remoteMemory,
            libraryPath,
            byteCount,
            &bytesWritten) ||
        bytesWritten != byteCount) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibraryA = reinterpret_cast<PAPCFUNC>(GetProcAddress(kernel32, "LoadLibraryA"));
    if (loadLibraryA == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    if (QueueUserAPC(
            loadLibraryA,
            static_cast<HANDLE>(mainThreadHandle),
            reinterpret_cast<ULONG_PTR>(remoteMemory)) == 0) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}

const char* GetHookLibraryPath() {
    return g_hookLibraryPath;
}

bool InstallProcessSpawnHooks() {
    if (g_spawnHooksInstalled.load()) {
        return true;
    }

    CacheHookLibraryPath();

    if (g_hookLibraryPath[0] == '\0') {
        DebugLogLine("[fx-hook] Failed to resolve hook library path");
        return false;
    }

    if (!EnsureHookEngineInitialized()) {
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        return false;
    }

    void* createProcessW = reinterpret_cast<void*>(GetProcAddress(kernel32, "CreateProcessW"));
    if (!CreateAndEnableHook(
            createProcessW,
            reinterpret_cast<void*>(&HookedCreateProcessW),
            reinterpret_cast<void**>(&g_originalCreateProcessW),
            "CreateProcessW")) {
        DebugLogLine("[fx-hook] Failed to install CreateProcessW hook");
        return false;
    }

    void* createProcessA = reinterpret_cast<void*>(GetProcAddress(kernel32, "CreateProcessA"));
    if (!CreateAndEnableHook(
            createProcessA,
            reinterpret_cast<void*>(&HookedCreateProcessA),
            reinterpret_cast<void**>(&g_originalCreateProcessA),
            "CreateProcessA")) {
        DebugLogLine("[fx-hook] Failed to install CreateProcessA hook");
        return false;
    }

    DebugLogLine("[fx-hook] Process spawn hook installed");
    g_spawnHooksInstalled.store(true);
    NotifyHookStageInstalled(HookInstallStage::ProcessSpawn);
    return true;
}
