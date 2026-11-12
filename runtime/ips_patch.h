// SPDX-License-Identifier: MIT
// Standard IPS (PATCH/EOF) and IPS32 (IPS32/EEOF), including zero bytes and RLE.
// Patches must fit the existing uncompressed module; validate every record before changing it.
#pragma once
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int sw_ips_error(char *why, size_t size, const char *message) {
    if (why && size) snprintf(why, size, "%s", message);
    return -1;
}
static uint32_t sw_ips_be(const uint8_t *p, size_t n) {
    uint32_t value = 0;
    for (size_t i = 0; i < n; i++) value = (value << 8) | p[i];
    return value;
}
static int sw_ips_records(const uint8_t *patch, size_t size, uint8_t *target, size_t length,
                          size_t protected_size, bool wide, bool write, char *why, size_t why_size) {
    size_t pos = 5, address_size = wide ? 4 : 3;
    const char *end = wide ? "EEOF" : "EOF";
    int records = 0;
    for (;;) {
        if (size - pos < address_size) return sw_ips_error(why, why_size, "missing IPS end marker");
        if (!memcmp(patch + pos, end, address_size)) {
            if (pos + address_size != size) return sw_ips_error(why, why_size, "IPS resizing/trailing data is unsupported");
            return records;
        }
        uint32_t offset = sw_ips_be(patch + pos, address_size);
        pos += address_size;
        if (size - pos < 2) return sw_ips_error(why, why_size, "truncated IPS record length");
        size_t count = sw_ips_be(patch + pos, 2); pos += 2;
        bool rle = count == 0;
        const uint8_t *data = patch + pos;
        if (rle) {
            if (size - pos < 3) return sw_ips_error(why, why_size, "truncated IPS RLE record");
            count = sw_ips_be(patch + pos, 2); data += 2; pos += 3;
            if (!count) return sw_ips_error(why, why_size, "empty IPS RLE record");
        } else {
            if (count > size - pos) return sw_ips_error(why, why_size, "truncated IPS record data");
            pos += count;
        }
        if (offset < protected_size) return sw_ips_error(why, why_size, "IPS record changes protected NSO header");
        if ((size_t)offset > length || count > length - (size_t)offset)
            return sw_ips_error(why, why_size, "IPS record reaches beyond module");
        if (records == INT_MAX) return sw_ips_error(why, why_size, "too many IPS records");
        if (write) {
            if (rle) memset(target + offset, *data, count);
            else memcpy(target + offset, data, count);
        }
        records++;
    }
}
static int sw_ips_apply(const uint8_t *patch, size_t size, uint8_t *target, size_t length,
                        size_t protected_size, char *why, size_t why_size) {
    if (why && why_size) why[0] = 0;
    if (!patch || size < 5 || !target ||
        (memcmp(patch, "PATCH", 5) && memcmp(patch, "IPS32", 5)))
        return sw_ips_error(why, why_size, "expected standard IPS or IPS32 header");
    bool wide = !memcmp(patch, "IPS32", 5);
    int records = sw_ips_records(patch, size, target, length, protected_size, wide, false, why, why_size);
    if (records < 0) return records;
    return sw_ips_records(patch, size, target, length, protected_size, wide, true, why, why_size);
}
