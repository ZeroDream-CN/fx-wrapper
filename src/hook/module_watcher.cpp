#include "module_watcher.h"

#include <windows.h>
#include <winternl.h>

#include <atomic>
#include <cwctype>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#ifndef NTSTATUS
using NTSTATUS = LONG;
#endif

#ifndef _LDR_DLL_NOTIFICATION_DATA_DEFINED
#define _LDR_DLL_NOTIFICATION_DATA_DEFINED
typedef struct _LDR_DLL_NOTIFICATION_DATA {
    union {
        struct {
            const UNICODE_STRING FullDllName;
            const UNICODE_STRING BaseDllName;
            PVOID DllBase;
            ULONG SizeOfImage;
        } Loaded;
    } DllInfo;
} LDR_DLL_NOTIFICATION_DATA, *PLDR_DLL_NOTIFICATION_DATA;
#endif

namespace {

constexpr wchar_t kTargetModuleName[] = L"citizen-scripting-core.dll";
constexpr ULONGLONG kPollIntervalMs = 100;
constexpr ULONGLONG kPollTimeoutMs = 60'000;

using PLDR_DLL_NOTIFICATION_FUNCTION = VOID(CALLBACK*)(ULONG, const LDR_DLL_NOTIFICATION_DATA*, PVOID);

using PLDR_REGISTER_DLL_NOTIFICATION = NTSTATUS(NTAPI*)(ULONG, PLDR_DLL_NOTIFICATION_FUNCTION, PVOID, PVOID*);
using PLDR_UNREGISTER_DLL_NOTIFICATION = NTSTATUS(NTAPI*)(PVOID);

std::function<void()> g_onModuleLoaded;
std::atomic<bool> g_moduleHandled{false};
std::atomic<bool> g_stopRequested{false};
PVOID g_notificationCookie = nullptr;
std::thread g_pollThread;

bool EndsWithIgnoreCase(std::wstring_view value, std::wstring_view suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }

    const size_t offset = value.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (towlower(value[offset + i]) != towlower(suffix[i])) {
            return false;
        }
    }

    return true;
}

bool IsTargetModuleLoaded() {
    HMODULE module = GetModuleHandleW(kTargetModuleName);
    return module != nullptr;
}

void InvokeModuleLoadedOnce() {
    bool expected = false;
    if (!g_moduleHandled.compare_exchange_strong(expected, true)) {
        return;
    }

    // Exports may not be bound immediately after the module load notification.
    Sleep(200);

    if (g_onModuleLoaded) {
        g_onModuleLoaded();
    }
}

void CALLBACK DllNotificationCallback(ULONG notificationReason, const LDR_DLL_NOTIFICATION_DATA* data, PVOID) {
    if (notificationReason != 1 || data == nullptr) {
        return;
    }

    const UNICODE_STRING& baseName = data->DllInfo.Loaded.BaseDllName;
    if (baseName.Buffer == nullptr || baseName.Length == 0) {
        return;
    }

    const std::wstring_view moduleName(baseName.Buffer, baseName.Length / sizeof(wchar_t));
    if (EndsWithIgnoreCase(moduleName, kTargetModuleName)) {
        InvokeModuleLoadedOnce();
    }
}

void PollForTargetModule() {
    const ULONGLONG startTick = GetTickCount64();
    while (!g_stopRequested.load() && !g_moduleHandled.load()) {
        if (IsTargetModuleLoaded()) {
            InvokeModuleLoadedOnce();
            return;
        }

        if (GetTickCount64() - startTick >= kPollTimeoutMs) {
            OutputDebugStringW(L"[fx-hook] Timed out waiting for citizen-scripting-core.dll\n");
            return;
        }

        Sleep(static_cast<DWORD>(kPollIntervalMs));
    }
}

bool RegisterDllNotification() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return false;
    }

    auto registerNotification = reinterpret_cast<PLDR_REGISTER_DLL_NOTIFICATION>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (registerNotification == nullptr) {
        return false;
    }

    const NTSTATUS status = registerNotification(0, DllNotificationCallback, nullptr, &g_notificationCookie);
    return status >= 0 && g_notificationCookie != nullptr;
}

void UnregisterDllNotification() {
    if (g_notificationCookie == nullptr) {
        return;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        g_notificationCookie = nullptr;
        return;
    }

    auto unregisterNotification = reinterpret_cast<PLDR_UNREGISTER_DLL_NOTIFICATION>(
        GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
    if (unregisterNotification != nullptr) {
        unregisterNotification(g_notificationCookie);
    }

    g_notificationCookie = nullptr;
}

}  // namespace

void StartModuleWatcher(std::function<void()> onModuleLoaded) {
    g_onModuleLoaded = std::move(onModuleLoaded);
    g_moduleHandled.store(false);
    g_stopRequested.store(false);

    if (IsTargetModuleLoaded()) {
        InvokeModuleLoadedOnce();
        return;
    }

    if (!RegisterDllNotification()) {
        OutputDebugStringW(L"[fx-hook] LdrRegisterDllNotification unavailable, using polling fallback\n");
    }

    g_pollThread = std::thread(PollForTargetModule);
}

void StopModuleWatcher() {
    g_stopRequested.store(true);

    if (g_pollThread.joinable()) {
        g_pollThread.join();
    }

    UnregisterDllNotification();
    g_onModuleLoaded = nullptr;
}
