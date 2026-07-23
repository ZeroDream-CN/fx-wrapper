#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef const uint64_t* (*GetCitizenLibsFn)(void*);

int main(void) {
    void* handle = dlopen(
        "/mnt/c/PrivateProject/fx-wrapper/server-linux/alpine/opt/cfx-server/libcitizen-scripting-lua.so",
        RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    auto getCitizenLibs =
        (GetCitizenLibsFn)dlsym(handle, "_ZN2fx16LuaScriptRuntime14GetCitizenLibsEv");
    auto getLuaLibs = (GetCitizenLibsFn)dlsym(handle, "_ZN2fx16LuaScriptRuntime10GetLuaLibsEv");
    if (!getCitizenLibs || !getLuaLibs) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return 1;
    }

    const uint64_t* tables[] = {getCitizenLibs(NULL), getLuaLibs(NULL)};
    const char* labels[] = {"CitizenLibs", "LuaLibs"};

    for (int t = 0; t < 2; ++t) {
        const uint64_t* table = tables[t];
        printf("=== %s @ %p\n", labels[t], (void*)table);
        for (int i = 0; i < 16; ++i) {
            const uint64_t* entry = table + (i * 6);
            if (entry[0] == 0) {
                printf("  [%d] end\n", i);
                break;
            }
            const char* name = (const char*)entry[0];
            printf("  [%d] %-12s fn=%p w2=%#" PRIx64 " w5=%#" PRIx64 "\n", i, name, (void*)entry[3], entry[2],
                entry[5]);
        }
    }

    dlclose(handle);
    return 0;
}
