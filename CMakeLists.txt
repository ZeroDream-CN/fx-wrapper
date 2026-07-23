cmake_minimum_required(VERSION 3.20)
project(fx-wrapper VERSION 1.0.2 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "fx-wrapper requires a 64-bit build.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FX_HOOK_COMMON_SOURCES
    src/hook/hook_entry.cpp
    src/hook/fx_log.cpp
    src/hook/binary_module.cpp
    src/hook/citizen_lua_api.cpp
    src/hook/lua_os_patch.cpp
    src/hook/module_watcher.cpp
    src/hook/hooks.cpp
    src/hook/node_hooks.cpp
    src/hook/update_check.cpp
)

if(WIN32)
    include(cmake/minhook.cmake)
    set(FX_LAUNCHER_PLATFORM_SOURCES src/platform/platform_win.cpp)
    set(FX_HOOK_PLATFORM_SOURCES
        src/platform/platform_win.cpp
        src/platform/hook_engine_win.cpp
        src/hook/hook_util_win.cpp
    )
else()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    include(cmake/subhook.cmake)
    set(FX_LAUNCHER_PLATFORM_SOURCES
        src/platform/platform_linux.cpp
        src/platform/linux_fxserver_launch.cpp
        src/platform/linux_thread.cpp
    )
    set(FX_HOOK_PLATFORM_SOURCES
        src/platform/platform_linux.cpp
        src/platform/linux_fxserver_launch.cpp
        src/platform/linux_thread.cpp
        src/platform/hook_engine_linux.cpp
        src/hook/hook_util_linux.cpp
    )
endif()

# ---------------------------------------------------------------------------
# FXWrapper — launcher
# ---------------------------------------------------------------------------
add_executable(fx-wrapper
    src/main.cpp
    src/process_launcher.cpp
    src/injector.cpp
    ${FX_LAUNCHER_PLATFORM_SOURCES}
)

target_include_directories(fx-wrapper PRIVATE src src/platform)

if(WIN32)
    target_compile_definitions(fx-wrapper PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(fx-wrapper PRIVATE kernel32)
else()
    target_link_libraries(fx-wrapper PRIVATE dl pthread)
endif()

if(MSVC)
    target_compile_options(fx-wrapper PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(fx-wrapper PRIVATE -Wall -Wextra -pedantic)
endif()

set_target_properties(fx-wrapper PROPERTIES OUTPUT_NAME "FXWrapper")

# ---------------------------------------------------------------------------
# fx-hook — injected hook library
# ---------------------------------------------------------------------------
add_library(fx-hook SHARED
    ${FX_HOOK_COMMON_SOURCES}
    ${FX_HOOK_PLATFORM_SOURCES}
)

target_include_directories(fx-hook PRIVATE src src/hook src/platform)

target_compile_definitions(fx-hook PRIVATE
    FX_HOOK_EXPORTS
    FX_WRAPPER_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_compile_definitions(fx-hook PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(fx-hook PRIVATE minhook kernel32 ntdll winhttp)
    set_target_properties(fx-hook PROPERTIES OUTPUT_NAME "fx-hook")
else()
    target_link_libraries(fx-hook PRIVATE subhook dl pthread)
    set_target_properties(fx-hook PROPERTIES
        OUTPUT_NAME "libfx-hook"
        PREFIX ""
    )
endif()

if(MSVC)
    target_compile_options(fx-hook PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(fx-hook PRIVATE -Wall -Wextra -pedantic)
endif()
