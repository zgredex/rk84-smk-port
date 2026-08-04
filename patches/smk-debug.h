#pragma once

#include <stdio.h>

#if DEBUG == 1
#define dprintf(...)                    \
    do {                                \
        if (DEBUG) printf(__VA_ARGS__); \
    } while (0)
#else
#define dprintf(...) do { } while (0)
#endif
