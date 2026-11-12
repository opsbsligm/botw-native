// SPDX-License-Identifier: MIT
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include "../runtime/mods.c"

const char *rt_data_root(void) { return "."; }
typedef struct { uint8_t data[1024]; size_t size; bool wide; } Patch;
static void append(Patch *p, const void *data, size_t n) {
    assert(n <= sizeof p->data - p->size); memcpy(p->data + p->size, data, n); p->size += n;
}
static void number(Patch *p, uint32_t value, unsigned size) {
    for (unsigned i = size; i; i--) { uint8_t b = (uint8_t)(value >> (8 * (i - 1))); append(p, &b, 1); }
}
static Patch begin_patch(bool wide) {
    Patch p = {.wide = wide}; append(&p, wide ? "IPS32" : "PATCH", 5); return p;
}
static void literal(Patch *p, uint32_t offset, const uint8_t *bytes, size_t n) {
    number(p, offset, p->wide ? 4 : 3); number(p, (uint32_t)n, 2); append(p, bytes, n);
}
static void repeat(Patch *p, uint32_t offset, uint16_t n, uint8_t value) {
    number(p, offset, p->wide ? 4 : 3); number(p, 0, 2); number(p, n, 2); append(p, &value, 1);
}
static void finish(Patch *p) { append(p, p->wide ? "EEOF" : "EOF", p->wide ? 4 : 3); }
static void reject(const Patch *p) {
    uint8_t target[512], before[512]; memset(target, 0x90, sizeof target); memcpy(before, target, sizeof target);
    char why[128]; assert(sw_ips_apply(p->data, p->size, target, sizeof target, 0x100, why, sizeof why) == -1);
    assert(*why && !memcmp(before, target, sizeof target));
}
static void write_patch(const Patch *p) {
    FILE *f = fopen("synthetic.ips", "wb"); assert(f);
    assert(fwrite(p->data, 1, p->size, f) == p->size); assert(!fclose(f));
}
static void put32(uint8_t *data, size_t offset, uint32_t value) { memcpy(data + offset, &value, 4); }
static void module_io(uint8_t data[512], bool write) {
    FILE *f = fopen("extracted/exefs/main", write ? "wb" : "rb"); assert(f);
    assert((write ? fwrite(data, 1, 512, f) : fread(data, 1, 512, f)) == 512); assert(!fclose(f));
}
static void integration(void) {
    assert(!mkdir("extracted", 0700)); assert(!mkdir("extracted/exefs", 0700));
    assert(!mkdir("mods", 0700)); assert(!mkdir("mods/demo", 0700));
    uint8_t original[512] = {0}, got[512]; memcpy(original, "NSO0", 4); memset(original + 0x100, 0x90, 0x100);
    memset(original + 0x40, 0x11, 32); put32(original, 0x10, 0x100); put32(original, 0x14, 0x80);
    put32(original, 0x18, 0x100); put32(original, 0x60, 0x100);
    uint32_t seg[3][3], bss;
    assert(nso_sections(original, sizeof original, seg, &bss) && seg[0][1] == 0x80 && seg[0][2] == 0x100);
    uint8_t unaligned[513]; memcpy(unaligned + 1, original, 512);
    assert(nso_sections(unaligned + 1, 512, seg, &bss));
    put32(unaligned + 1, 0x10, 500); assert(!nso_sections(unaligned + 1, 512, seg, &bss));
    module_io(original, true);
    const uint8_t value = 0;
    Patch p = begin_patch(false); literal(&p, 0x100, &value, 1); literal(&p, 0x200, &value, 1); finish(&p);
    write_patch(&p); assert(!rename("synthetic.ips", "mods/demo/main.ips"));
    FILE *f = fopen("extracted/.mods-patched", "w"); assert(f); fputs("previous completion\n", f); fclose(f);
    char why[512], record[1024];
    assert(mods_exefs_apply("extracted", why, sizeof why, record, sizeof record) == -1 && *why && !*record);
    module_io(got, false); assert(!memcmp(got, original, 512)); assert(access("extracted/.mods-patched", F_OK));
    p = begin_patch(true); literal(&p, 0x100, &value, 1); finish(&p);
    write_patch(&p); assert(!rename("synthetic.ips", "mods/demo/main.ips"));
    assert(mods_exefs_apply("extracted", why, sizeof why, record, sizeof record) == 1 && !*why && *record);
    module_io(got, false); assert(got[0x100] == 0 && !memcmp(got, original, 0x100));
    mods_mark_built("extracted"); assert(!mods_need_rebuild("extracted"));
    // Fingerprints use stored length in compressed input, not its larger decoded size.
    put32(original, 0x0C, 1); put32(original, 0x18, 1024); put32(original, 0x60, 4);
    assert(nso_sections(original, 512, seg, &bss) && seg[0][2] == 4); module_io(original, true);
    assert(mods_need_rebuild("extracted"));
    assert(mods_exefs_apply("extracted", why, sizeof why, record, sizeof record) == -1 && strstr(why, "uncompressed"));
    module_io(got, false); assert(!memcmp(got, original, 512) && access("extracted/.mods-patched", F_OK));
}
int main(void) {
    const uint8_t values[] = {0x11, 0, 0x22, 0x33}, overwrite = 0x44;
    uint8_t target[512]; char why[512];
    for (int wide = 0; wide < 2; wide++) {
        Patch p = begin_patch(wide); literal(&p, 0x100, values, sizeof values);
        repeat(&p, 0x104, 3, 0); literal(&p, 0x102, &overwrite, 1); finish(&p);
        memset(target, 0x90, sizeof target);
        assert(sw_ips_apply(p.data, p.size, target, sizeof target, 0x100, why, sizeof why) == 3 && !*why);
        assert(target[0x100] == 0x11 && target[0x101] == 0 && target[0x102] == 0x44 && target[0x103] == 0x33);
        assert(target[0x104] == 0 && target[0x105] == 0 && target[0x106] == 0 && target[0x107] == 0x90);
        for (int i = 0; i < 0x100; i++) assert(target[i] == 0x90);
        // Real runtime file reader/dispatch must accept both standard headers, not only the legacy dialect.
        write_patch(&p); memset(target, 0x90, sizeof target); memcpy(target, "NSO0", 4); memset(target + 0x0C, 0, 4);
        assert(apply_ips("synthetic.ips", "main", target, sizeof target, why, sizeof why) == 3);
        assert(target[0x101] == 0 && target[0x102] == 0x44);
        target[0x0C] = 1; target[0x100] = 0x90;
        assert(apply_ips("synthetic.ips", "main", target, sizeof target, why, sizeof why) == -1);
        assert(strstr(why, "uncompressed") && target[0x100] == 0x90);
        // A malformed late record must not leave even the first valid record applied.
        Patch bad = p; bad.size -= wide ? 4 : 3; reject(&bad);
        uint8_t extra = 0; append(&p, &extra, 1); reject(&p);
        bad = begin_patch(wide); literal(&bad, 0x100, values, 4); literal(&bad, 0x1FF, values, 4); finish(&bad); reject(&bad);
        bad = begin_patch(wide); literal(&bad, 0xFF, values, 4); finish(&bad); reject(&bad);
        bad = begin_patch(wide); repeat(&bad, 0x100, 0, 0); finish(&bad); reject(&bad);
        bad = begin_patch(wide); literal(&bad, 0x100, values, 4); bad.size--; reject(&bad);
        bad = begin_patch(wide); finish(&bad); memset(target, 0x90, sizeof target);
        assert(sw_ips_apply(bad.data, bad.size, target, sizeof target, 0x100, why, sizeof why) == 0);
    }
    Patch large = begin_patch(true); literal(&large, UINT32_MAX, values, 4); finish(&large); reject(&large);
    uint8_t *big = malloc((1u << 24) + 4); assert(big); memset(big, 0x90, (1u << 24) + 4);
    large = begin_patch(true); literal(&large, 1u << 24, values, 4); finish(&large);
    assert(sw_ips_apply(large.data, large.size, big, (1u << 24) + 4, 0x100, why, sizeof why) == 1);
    assert(!memcmp(big + (1u << 24), values, 4)); free(big);
    assert(!remove("synthetic.ips"));
    integration();
    puts("Standard IPS/IPS32: literal zero/RLE/overlap/large offset, runtime dispatch and atomic rejection passed");
    return 0;
}
