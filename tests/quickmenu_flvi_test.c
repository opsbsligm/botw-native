/* SPDX-License-Identifier: MIT */
/* The FLAN quick-menu step parser: extract it from runtime/services/fs.c (between its TEST markers) and verify it
 * against a minimal hand-built BFLAN and a real one read from the fixtures dir when present. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bflan_step_flvi.inc"

static void u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xff); p[1] = (uint8_t)(v >> 8); }
static void u32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void key(uint8_t *p, float value, uint16_t frame) {
    float v = value;
    memcpy(p, &v, 4);
    u16(p + 4, frame);
}

// A minimal FLAN: one pai1 section, one pane "P_X", one FLVI track, one step group (2 keys: 0.0@0 -> 1.0@1).
static size_t build_flan(uint8_t *b, float v1) {
    size_t at = 0x14;  // (the FLAN header is 0x14 bytes; its section count is the u16 at 0x10)
    memcpy(b, "FLAN\xff\xfe", 6); u16(b + 6, 0x14); u32(b + 0xC, 0); u16(b + 0x10, 1);
    size_t sec = at;
    memcpy(b + at, "pai1", 4);
    u16(b + at + 14, 1);                 // ents
    size_t tbl = at + 0x14;              // offset table first (relative to the section start)
    u32(b + at + 16, 0x14);
    size_t p = tbl + 4;                  // then the pane entry: 28 name, u32 track count, a u32 per track
    u32(b + tbl, (uint32_t)(p - at));    // (the entry offset is measured from the section start, like the real file)
    memcpy(b + p, "P_X", 3); u32(b + p + 28, 1);
    size_t f = p + 36;                   // FLVI section (past the pane's track table)
    u32(b + p + 32, (uint32_t)(f - p));
    memcpy(b + f, "FLVI", 4); u32(b + f + 4, 1);  // one group
    size_t g = f + 12;
    u32(b + f + 8, (uint32_t)(g - f));   // group offset: relative to the FLVI section
    b[g + 2] = 1; u16(b + g + 4, 2); u32(b + g + 8, 12);  // step curve, 2 keys, table at group + 12
    key(b + g + 12, 0.0f, 0);
    key(b + g + 12 + 8, v1, 1);
    size_t end = g + 12 + 16;
    u32(b + sec + 4, (uint32_t)(end - sec));
    return end;
}

int main(void) {
    uint8_t b[512] = {0};
    size_t n;
    // the real shape: second key 1.0 at frame 1 -> found at the second key's value
    n = build_flan(b, 1.0f);
    size_t off;
    assert(bflan_step_flvi(b, n, "P_X", &off) == 0);
    float v0, v1;
    memcpy(&v0, b + off - 8, 4); memcpy(&v1, b + off, 4);
    assert(v0 == 0.0f && v1 == 1.0f);
    // the other pane: not found
    assert(bflan_step_flvi(b, n, "P_Y", &off) == -1);
    // not a step to frame 1 (second key's frame 0): not found
    n = build_flan(b, 1.0f);
    uint16_t *fr = (uint16_t *)(b + off + 4);
    *fr = 0;
    assert(bflan_step_flvi(b, n, "P_X", &off) == -1);
    // second key's value not 1.0 but frame 1: still the step (the value is what gets patched)
    n = build_flan(b, 0.5f);
    assert(bflan_step_flvi(b, n, "P_X", &off) == 0);
    memcpy(&v1, b + off, 4);
    assert(v1 == 0.5f);
    // no FLVI track
    {
        uint8_t c[512] = {0};
        n = build_flan(c, 1.0f);
        const uint8_t *mv = memchr(c, 'F', n);
        (void)mv;
        for (size_t i = 0; i + 4 <= n; i++)
            if (!memcmp(c + i, "FLVI", 4)) { c[i] = 'X'; break; }
        assert(bflan_step_flvi(c, n, "P_X", &off) == -1);
    }
    // real data: the Deck release's lane (tests/fixtures/ when shipped with the source)
    {
        FILE *fp = fopen("tests/fixtures/MainShortCut_00_HoldShortCut.bflan", "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
            assert(sz > 0 && sz < (1u << 20));
            uint8_t *d = malloc(sz);
            assert(fread(d, 1, sz, fp) == (size_t)sz);
            fclose(fp);
            size_t r;
            assert(bflan_step_flvi(d, sz, "N_DmcCapture_00", &r) == 0);
            float a, c;
            memcpy(&a, d + r - 8, 4); memcpy(&c, d + r, 4);
            assert(a == 0.0f && c == 1.0f);
            free(d);
        }
    }
    printf("quickmenu flvi ok\n");
    return 0;
}
