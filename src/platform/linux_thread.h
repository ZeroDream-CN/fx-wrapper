#pragma once

#if !defined(_WIN32)

using DetachedThreadFn = void (*)();

void LaunchDetachedThread(DetachedThreadFn threadFn);

#endif
