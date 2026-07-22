#pragma once

#include <windows.h>

struct lua_State;

struct CitizenLuaApi {
    using LuaPushStringFn = void(__cdecl*)(lua_State*, const char*);

    LuaPushStringFn lua_pushstring = nullptr;
};

bool ResolveCitizenLuaApi(HMODULE module, void* sandboxExecute, CitizenLuaApi& outApi);
void* FindSystemLibsFunction(HMODULE module, const char* functionName);

const char* ReadLuaOptStringArg1(lua_State* L);
const char* ReadLuaStringArgFromTop(lua_State* L, int offsetFromTop);
void PushLuaBoolean(lua_State* L, bool value);
void PushLuaNil(lua_State* L);
void PushLuaInteger(lua_State* L, long long value);
void PushLuaString(CitizenLuaApi& api, lua_State* L, const char* value);
int PushLuaExecResult(CitizenLuaApi& api, lua_State* L, int stat);
int PushLuaFileResult(CitizenLuaApi& api, lua_State* L, bool success, const char* path);
int PushLuaRenameResult(CitizenLuaApi& api, lua_State* L, bool success, const char* from, const char* to);
