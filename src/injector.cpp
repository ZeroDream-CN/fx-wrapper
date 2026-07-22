#include "injector.h"

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
