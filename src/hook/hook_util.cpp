#include "hook_util.h"
#include "fx_log.h"

#include <cwchar>
#include <string>
#include <string_view>

namespace {

wchar_t g_hookDllPath[MAX_PATH]{};

void CacheHookDllPath() {
    if (g_hookDllPath[0] != L'\0') {
        return;
    }

    HMODULE selfModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CacheHookDllPath),
            &selfModule)) {
        return;
    }

    const DWORD length = GetModuleFileNameW(selfModule, g_hookDllPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        g_hookDllPath[0] = L'\0';
    }
}

std::wstring GetBaseName(const wchar_t* path) {
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

std::wstring ExtractFirstCommandLineToken(const wchar_t* commandLine) {
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

bool IsFxServerLaunch(LPCWSTR applicationName, LPCWSTR commandLine) {
    constexpr wchar_t kFxServerExe[] = L"FXServer.exe";

    if (applicationName != nullptr && applicationName[0] != L'\0') {
        return _wcsicmp(GetBaseName(applicationName).c_str(), kFxServerExe) == 0;
    }

    const std::wstring firstToken = ExtractFirstCommandLineToken(commandLine);
    if (!firstToken.empty()) {
        return _wcsicmp(GetBaseName(firstToken.c_str()).c_str(), kFxServerExe) == 0;
    }

    if (commandLine != nullptr && commandLine[0] != L'\0') {
        return wcsstr(commandLine, L"FXServer.exe") != nullptr || wcsstr(commandLine, L"fxserver.exe") != nullptr;
    }

    return false;
}

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

CreateProcessWFn g_originalCreateProcessW = nullptr;

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

    if (lpProcessInformation == nullptr || !IsFxServerLaunch(lpApplicationName, lpCommandLine)) {
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

    const wchar_t* hookDllPath = GetHookDllPath();
    if (hookDllPath != nullptr && hookDllPath[0] != L'\0') {
        if (InjectDllApc(lpProcessInformation->hProcess, lpProcessInformation->hThread, hookDllPath)) {
            OutputDebugStringW(L"[fx-hook] Propagated hook DLL to child FXServer.exe\n");
        } else {
            OutputDebugStringW(L"[fx-hook] Failed to propagate hook DLL to child FXServer.exe\n");
        }
    }

    if (!alreadySuspended) {
        ResumeThread(lpProcessInformation->hThread);
    }

    return TRUE;
}

}  // namespace

bool EnsureMinHookInitialized() {
    static bool initialized = false;
    if (initialized) {
        return true;
    }

    if (MH_Initialize() != MH_OK) {
        OutputDebugStringW(L"[fx-hook] MH_Initialize failed\n");
        return false;
    }

    initialized = true;
    return true;
}

bool CreateAndEnableHook(void* target, void* detour, void** original, const char* symbolName) {
    if (target == nullptr) {
        OutputDebugStringA("[fx-hook] Export not found: ");
        OutputDebugStringA(symbolName);
        OutputDebugStringA("\n");
        return false;
    }

    if (MH_CreateHook(target, detour, original) != MH_OK) {
        OutputDebugStringA("[fx-hook] MH_CreateHook failed: ");
        OutputDebugStringA(symbolName);
        OutputDebugStringA("\n");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        OutputDebugStringA("[fx-hook] MH_EnableHook failed: ");
        OutputDebugStringA(symbolName);
        OutputDebugStringA("\n");
        return false;
    }

    OutputDebugStringA("[fx-hook] Hook installed: ");
    OutputDebugStringA(symbolName);
    OutputDebugStringA("\n");
    return true;
}

const wchar_t* GetHookDllPath() {
    return g_hookDllPath;
}

bool InjectDllApc(HANDLE processHandle, HANDLE mainThreadHandle, const wchar_t* dllPath) {
    if (processHandle == nullptr || mainThreadHandle == nullptr || dllPath == nullptr || dllPath[0] == L'\0') {
        return false;
    }

    const size_t byteCount = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remoteMemory = VirtualAllocEx(processHandle, nullptr, byteCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemory == nullptr) {
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(processHandle, remoteMemory, dllPath, byteCount, &bytesWritten) ||
        bytesWritten != byteCount) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    auto loadLibraryW = reinterpret_cast<PAPCFUNC>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibraryW == nullptr) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    if (QueueUserAPC(loadLibraryW, mainThreadHandle, reinterpret_cast<ULONG_PTR>(remoteMemory)) == 0) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}

bool InstallProcessSpawnHooks() {
    CacheHookDllPath();

    if (g_hookDllPath[0] == L'\0') {
        OutputDebugStringW(L"[fx-hook] Failed to resolve fx-hook.dll path\n");
        return false;
    }

    if (!EnsureMinHookInitialized()) {
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        return false;
    }

    void* createProcessW = reinterpret_cast<void*>(GetProcAddress(kernel32, "CreateProcessW"));
    if (!CreateAndEnableHook(
            createProcessW,
            reinterpret_cast<void*>(&HookedCreateProcessW),
            reinterpret_cast<void**>(&g_originalCreateProcessW),
            "CreateProcessW")) {
        return false;
    }

    OutputDebugStringW(L"[fx-hook] Process spawn hook installed\n");
    NotifyHookStageInstalled(HookInstallStage::ProcessSpawn);
    return true;
}
