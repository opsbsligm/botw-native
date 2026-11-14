#include "rstb_budget.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}

static bool shape(const uint8_t *d, size_t n) {
    if (!d || n < 12 || memcmp(d, "RSTB", 4)) return false;
    uint32_t nc = rd32(d + 4), nn = rd32(d + 8);
    uint64_t end = 12 + (uint64_t)nc * 8 + (uint64_t)nn * 132;
    return end <= n;
}
bool rstb_budget_valid(const uint8_t *d, size_t n) {
    if (!shape(d, n)) return false;
    uint32_t nc = rd32(d + 4), nn = rd32(d + 8);
    for (uint32_t i = 1; i < nc; i++)
        if (rd32(d + 12 + (size_t)(i - 1) * 8) >= rd32(d + 12 + (size_t)i * 8)) return false;
    for (uint32_t i = 0; i < nn; i++) {
        const uint8_t *name = d + 12 + (size_t)nc * 8 + (size_t)i * 132;
        if (!name[0] || !memchr(name, 0, 128)) return false;
    }
    return true;
}

// The guest searches the sorted CRC table first, then the collision-name table.
static size_t entry(const uint8_t *d, const char *path) {
    uint32_t nc = rd32(d + 4), nn = rd32(d + 8);
    uint32_t hash = (uint32_t)crc32(0, (const Bytef *)path, (uInt)strlen(path));
    for (uint32_t lo = 0, hi = nc; lo < hi;) {
        uint32_t mid = lo + (hi - lo) / 2, h = rd32(d + 12 + (size_t)mid * 8);
        if (h == hash) return 12 + (size_t)mid * 8 + 4;
        if (h < hash) lo = mid + 1;
        else hi = mid;
    }
    for (uint32_t i = 0; i < nn; i++) {
        size_t off = 12 + (size_t)nc * 8 + (size_t)i * 132;
        if (!strcmp((const char *)d + off, path)) return off + 128;
    }
    return 0;
}

bool rstb_budget_get(const uint8_t *d, size_t n, const char *path, uint32_t *value) {
    if (!path || !value || !shape(d, n)) return false;
    size_t off = entry(d, path);
    if (!off) return false;
    *value = rd32(d + off);
    return true;
}

bool rstb_budget_raise(uint8_t **data, size_t *size, const char *path, uint32_t floor) {
    if (!data || !size || !path || !*path || !shape(*data, *size)) return false;
    uint8_t *d = *data;
    size_t off = entry(d, path);
    if (off) {
        if (rd32(d + off) < floor) wr32(d + off, floor);
        return true;
    }
    uint32_t nc = rd32(d + 4), nn = rd32(d + 8);
    uint32_t hash = (uint32_t)crc32(0, (const Bytef *)path, (uInt)strlen(path));
    bool collision = false;
    for (uint32_t i = 0; i < nn; i++) {
        const char *name = (const char *)d + 12 + (size_t)nc * 8 + (size_t)i * 132;
        if ((uint32_t)crc32(0, (const Bytef *)name, (uInt)strlen(name)) == hash) collision = true;
    }
    if (collision && strlen(path) >= 128) return false;
    size_t extra = collision ? 132 : 8;
    if (*size > SIZE_MAX - extra || (collision ? nn : nc) == UINT32_MAX) return false;
    uint8_t *out = malloc(*size + extra);
    if (!out) return false;
    if (collision) {
        off = 12 + (size_t)nc * 8 + (size_t)nn * 132;
        memcpy(out, d, off);
        memset(out + off, 0, 132);
        memcpy(out + off, path, strlen(path));
        wr32(out + off + 128, floor);
        memcpy(out + off + extra, d + off, *size - off);
        wr32(out + 8, nn + 1);
    } else {
        uint32_t i = 0;
        while (i < nc && rd32(d + 12 + (size_t)i * 8) < hash) i++;
        off = 12 + (size_t)i * 8;
        memcpy(out, d, off);
        wr32(out + off, hash);
        wr32(out + off + 4, floor);
        memcpy(out + off + extra, d + off, *size - off);
        wr32(out + 4, nc + 1);
    }
    free(d);
    *data = out;
    *size += extra;
    return true;
}

bool rstb_budget_growth(uint32_t original, uint64_t growth, uint32_t *floor) {
    // ceil(growth, 32) + 32, checked before either rounding or addition can wrap.
    if (!floor || growth > UINT32_MAX - 63u) return false;
    uint64_t v = (uint64_t)original + ((growth + 31) & ~UINT64_C(31)) + 32;
    if (v > UINT32_MAX) return false;
    *floor = (uint32_t)v;
    return true;
}
