include(FetchContent)

set(SUBHOOK_STATIC ON CACHE BOOL "" FORCE)
set(SUBHOOK_TESTS OFF CACHE BOOL "" FORCE)
set(SUBHOOK_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    subhook
    URL https://github.com/tianocore/edk2-subhook/archive/refs/heads/main.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(subhook)
