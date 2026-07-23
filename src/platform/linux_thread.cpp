#include "linux_thread.h"

#include <pthread.h>

void LaunchDetachedThread(const DetachedThreadFn threadFn) {
    if (threadFn == nullptr) {
        return;
    }

    struct ThreadContext {
        DetachedThreadFn fn;
    };

    auto* context = new ThreadContext{threadFn};

    pthread_t thread{};
    pthread_attr_t attributes{};
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    const auto trampoline = +[](void* arg) -> void* {
        auto* threadContext = static_cast<ThreadContext*>(arg);
        const DetachedThreadFn fn = threadContext->fn;
        delete threadContext;
        fn();
        return nullptr;
    };

    if (pthread_create(&thread, &attributes, trampoline, context) != 0) {
        delete context;
    }

    pthread_attr_destroy(&attributes);
}
