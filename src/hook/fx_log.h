#pragma once

#include <cstdint>

enum class HookInstallStage {
    ProcessSpawn,
    ScriptingCore,
    ScriptingNode,
    LuaOs,
};

void NotifyHookStageInstalled(HookInstallStage stage);
void LogFxMessage(const char* message);
bool WaitForLogReady(std::uint32_t maxWaitMs = 30000);
