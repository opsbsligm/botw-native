/* SPDX-License-Identifier: MIT */
/* Application memory reported to the guest; actual SetHeapSize stays bounded. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SW_DEFAULT_APP_MEMORY UINT64_C(0xcd500000)
#define SW_SDK7_HD_EXTRA_MEMORY (UINT64_C(160) * 1024 * 1024)

static inline uint64_t sdk7_application_memory(bool sdk7, const char *option) {
#if defined(__linux__)
    if (sdk7 && option && option[0] == '1' && option[1] == '\0')
        return SW_DEFAULT_APP_MEMORY + SW_SDK7_HD_EXTRA_MEMORY;
#else
    (void)sdk7;
    (void)option;
#endif
    return SW_DEFAULT_APP_MEMORY;
}
