cmake_minimum_required(VERSION 3.20)
project(fx-wrapper VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "fx-wrapper requires a 64-bit (x64) build.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/minhook.cmake)

# ---------------------------------------------------------------------------
# FXWrapper.exe — launcher
# ---------------------------------------------------------------------------
add_executable(fx-wrapper
    src/main.cpp
    src/process_launcher.cpp
    src/injector.cpp
)

target_include_directories(fx-wrapper PRIVATE src)

target_compile_definitions(fx-wrapper PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(fx-wrapper PRIVATE kernel32)

if(MSVC)
    target_compile_options(fx-wrapper PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(fx-wrapper PROPERTIES
    OUTPUT_NAME "FXWrapper"
)

# ---------------------------------------------------------------------------
# fx-hook.dll — injected hook library
# ---------------------------------------------------------------------------
add_library(fx-hook SHARED
    src/hook/dllmain.cpp
    src/hook/fx_log.cpp
    src/hook/hook_util.cpp
    src/hook/citizen_lua_api.cpp
    src/hook/lua_os_patch.cpp
    src/hook/module_watcher.cpp
    src/hook/hooks.cpp
    src/hook/update_check.cpp
)

target_include_directories(fx-hook PRIVATE src src/hook)

target_compile_definitions(fx-hook PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    FX_HOOK_EXPORTS
    FX_WRAPPER_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(fx-hook PRIVATE
    minhook
    kernel32
    ntdll
    winhttp
)

if(MSVC)
    target_compile_options(fx-hook PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(fx-hook PROPERTIES
    OUTPUT_NAME "fx-hook"
)
