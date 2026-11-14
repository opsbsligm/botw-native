/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "guest_memory_profile.h"

int main(void) {
    assert(sdk7_application_memory(true, NULL) == UINT64_C(0xcd500000));
    assert(sdk7_application_memory(false, "1") == UINT64_C(0xcd500000));
    const char *invalid[] = {"", "0", "01", "+1", "-1", "1 ", "1\n", "1x", "true", "2",
        "18446744073709551616"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        assert(sdk7_application_memory(true, invalid[i]) == UINT64_C(0xcd500000));
#if defined(__linux__)
    assert(sdk7_application_memory(true, "1") == UINT64_C(0xd7500000));
#else
    assert(sdk7_application_memory(true, "1") == UINT64_C(0xcd500000));
#endif
    puts("guest application memory profile passed");
    return 0;
}
