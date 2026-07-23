#pragma once

#include <cstdint>
#include <string>

bool InjectHookLibrary(void* processHandle, void* mainThreadHandle, const std::string& libraryPath, std::string& outError);
bool WaitForInjectedModule(void* processHandle, const std::string& libraryPath, std::uint32_t timeoutMs, std::string& outError);
