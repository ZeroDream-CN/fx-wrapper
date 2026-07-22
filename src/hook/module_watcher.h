#pragma once

#include <functional>

// Start watching for citizen-scripting-core.dll and invoke callback once loaded.
void StartModuleWatcher(std::function<void()> onModuleLoaded);

// Stop the module watcher and release notification registration.
void StopModuleWatcher();
