#pragma once

// Queue deferred installation of citizen-scripting-core.dll hooks.
void ScheduleScriptingHookInstall();

// Backwards-compatible entry point used by module watcher.
bool InstallScriptingHooks();
