#include "injector.h"

#include "platform/platform.h"

#if defined(_WIN32)

#include <windows.h>

#include <filesystem>
#include <tlhelp32.h>

namespace {

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

        Sleep(50);
    }

    return IsRemoteModuleLoaded(processId, moduleName);
}

}  // namespace

bool InjectHookLibrary(void* processHandle, void* mainThreadHandle, const std::string& libraryPath, std::string& outError) {
    if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE) {
        outError = "Invalid process handle for library injection.";
        return false;
    }

    if (mainThreadHandle == nullptr || mainThreadHandle == INVALID_HANDLE_VALUE) {
        outError = "Invalid main thread handle for library injection.";
        return false;
    }

    if (libraryPath.empty()) {
        outError = "Library path is empty.";
        return false;
    }

    const size_t byteCount = libraryPath.size() + 1;
    void* remoteMemory = VirtualAllocEx(
        static_cast<HANDLE>(processHandle),
        nullptr,
        byteCount,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (remoteMemory == nullptr) {
        outError = "VirtualAllocEx failed. GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(
            static_cast<HANDLE>(processHandle),
            remoteMemory,
            libraryPath.c_str(),
            byteCount,
            &bytesWritten) ||
        bytesWritten != byteCount) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        outError = "WriteProcessMemory failed. GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        outError = "GetModuleHandleA(kernel32.dll) failed.";
        return false;
    }

    auto loadLibraryA = reinterpret_cast<PAPCFUNC>(GetProcAddress(kernel32, "LoadLibraryA"));
    if (loadLibraryA == nullptr) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        outError = "GetProcAddress(LoadLibraryA) failed.";
        return false;
    }

    const DWORD apcResult =
        QueueUserAPC(loadLibraryA, static_cast<HANDLE>(mainThreadHandle), reinterpret_cast<ULONG_PTR>(remoteMemory));
    if (apcResult == 0) {
        VirtualFreeEx(static_cast<HANDLE>(processHandle), remoteMemory, 0, MEM_RELEASE);
        outError = "QueueUserAPC failed. GetLastError=" + std::to_string(GetLastError());
        return false;
    }

    return true;
}

bool WaitForInjectedModule(void* processHandle, const std::string& libraryPath, std::uint32_t timeoutMs, std::string& outError) {
    const std::string moduleName = std::filesystem::path(libraryPath).filename().string();
    if (WaitForRemoteModuleLoaded(static_cast<HANDLE>(processHandle), moduleName.c_str(), timeoutMs)) {
        return true;
    }

    outError = "Timed out waiting for injected library to load: " + moduleName;
    return false;
}

#else

bool InjectHookLibrary(void* /*processHandle*/, void* /*mainThreadHandle*/, const std::string& /*libraryPath*/, std::string& /*outError*/) {
    return true;
}

bool WaitForInjectedModule(void* /*processHandle*/, const std::string& /*libraryPath*/, std::uint32_t /*timeoutMs*/, std::string& /*outError*/) {
    return true;
}

#endif
