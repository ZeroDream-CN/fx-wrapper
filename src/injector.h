#pragma once

#include <windows.h>

#include <string>

// Inject a DLL via QueueUserAPC(LoadLibraryW) on the suspended main thread.
bool InjectDll(HANDLE processHandle, HANDLE mainThreadHandle, const std::wstring& dllPath, std::wstring& outError);
