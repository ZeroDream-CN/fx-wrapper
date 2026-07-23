#pragma once

#include "platform/platform.h"

#include <string>

bool InstallProcessSpawnHooks();
const char* GetHookLibraryPath();

#if defined(_WIN32)
bool InjectHookLibraryApc(void* processHandle, void* mainThreadHandle, const char* libraryPath);
#endif
