#pragma once

#include <windows.h>

#include <MinHook.h>

bool EnsureMinHookInitialized();
bool CreateAndEnableHook(void* target, void* detour, void** original, const char* symbolName);

const wchar_t* GetHookDllPath();
bool InjectDllApc(HANDLE processHandle, HANDLE mainThreadHandle, const wchar_t* dllPath);

bool InstallProcessSpawnHooks();
