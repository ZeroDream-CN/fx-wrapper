#pragma once

#include <windows.h>

#include <string>

// Inject a DLL via QueueUserAPC(LoadLibraryW) on the suspended main thread.
bool InjectDll(HANDLE processHandle, HANDLE mainThreadHandle, const std::wstring& dllPath, std::wstring& outError);

// Wait until the injected DLL appears in the target process module list.
bool WaitForInjectedModule(HANDLE processHandle, const std::wstring& dllPath, DWORD timeoutMs, std::wstring& outError);
