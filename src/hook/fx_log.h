#pragma once

enum class HookInstallStage {
    ProcessSpawn,
    ScriptingCore,
    LuaOs,
};

void NotifyHookStageInstalled(HookInstallStage stage);
