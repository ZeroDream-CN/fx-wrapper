#include "injector.h"

#include <filesystem>
#include <tlhelp32.h>

namespace {

bool IsRemoteModuleLoaded(DWORD processId, const wchar_t* moduleName) {
    if (processId == 0 || moduleName == nullptr || moduleName[0] == L'\0') {
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool WaitForRemoteModuleLoaded(HANDLE processHandle, const wchar_t* moduleName, DWORD timeoutMs) {
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

bool InjectDll(HANDLE processHandle, HANDLE mainThreadHandle, const std::wstring& dllPath, std::wstring& outError) {
    if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE) {
        outError = L"Invalid process handle for DLL injection.";
        return false;
    }

    if (mainThreadHandle == nullptr || mainThreadHandle == INVALID_HANDLE_VALUE) {
        outError = L"Invalid main thread handle for DLL injection.";
        return false;
    }

    if (dllPath.empty()) {
        outError = L"DLL path is empty.";
        return false;
    }

    const size_t byteCount = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMemory = VirtualAllocEx(processHandle, nullptr, byteCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemory == nullptr) {
        outError = L"VirtualAllocEx failed. GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(processHandle, remoteMemory, dllPath.c_str(), byteCount, &bytesWritten) ||
        bytesWritten != byteCount) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        outError = L"WriteProcessMemory failed. GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        outError = L"GetModuleHandleW(kernel32.dll) failed.";
        return false;
    }

    auto loadLibraryW = reinterpret_cast<PAPCFUNC>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (loadLibraryW == nullptr) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        outError = L"GetProcAddress(LoadLibraryW) failed.";
        return false;
    }

    const DWORD apcResult = QueueUserAPC(loadLibraryW, mainThreadHandle, reinterpret_cast<ULONG_PTR>(remoteMemory));
    if (apcResult == 0) {
        VirtualFreeEx(processHandle, remoteMemory, 0, MEM_RELEASE);
        outError = L"QueueUserAPC failed. GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    return true;
}

bool WaitForInjectedModule(HANDLE processHandle, const std::wstring& dllPath, DWORD timeoutMs, std::wstring& outError) {
    const std::wstring moduleName = std::filesystem::path(dllPath).filename().wstring();
    if (WaitForRemoteModuleLoaded(processHandle, moduleName.c_str(), timeoutMs)) {
        return true;
    }

    outError = L"Timed out waiting for injected DLL to load: " + moduleName;
    return false;
}
