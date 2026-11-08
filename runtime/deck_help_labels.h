/* World-only help labels must not share the menu A/B icon atlas. MIT. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

static uint16_t nd_help_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t nd_help_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t nd_help_hash(const uint8_t *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
static int nd_help_text_impl(uint8_t *b, size_t n, const char labels[4]) {
    static const char *const names[12] = {
        "T_Icon_00", "T_Icon_02", "T_Icon_03", "T_Icon_01", "T_Icon_12", "T_Icon_13",
        "T_Icon_28", "T_Icon_29", "T_Icon_25", "T_Icon_26", "T_IconXB_09", "T_IconXB_10"
    };
    static const uint16_t codes[12] = {0xe042,0xe040,0xe041,0xe043,0xe042,0xe040,0xe041,0xe043,0xe040,0xe041,0xe042,0xe040};
    if (!b || n < 20 || memcmp(b, "FLYT\xff\xfe", 6) || nd_help_u32(b + 12) != n) return 0;
    for (unsigned i = 0; i < 4; i++) if (!labels[i] || !strchr("ABXY", labels[i])) return 0;
    size_t sections[12], text[12]; float font_sizes[12][2]; unsigned seen = 0;
    size_t at = nd_help_u16(b + 6);
    if (at < 20 || at > n) return 0;
    while (at + 8 <= n) {
        size_t size = nd_help_u32(b + at + 4);
        if (size < 8 || size > n - at) return 0;
        if (!memcmp(b + at, "txt1", 4) && size >= 0x78) {
            if (!memchr(b + at + 12, 0, 24)) return 0;
            for (unsigned i = 0; i < 12; i++) if (!strcmp((char *)b + at + 12, names[i])) {
                size_t off = nd_help_u32(b + at + 0x64);
                if ((seen & (1u << i)) || nd_help_u16(b + at + 0x5a) != 1 || off < 0x78 || off > size - 4 ||
                    nd_help_u16(b + at + off) != codes[i] || nd_help_u16(b + at + off + 2)) return 0;
                memcpy(font_sizes[i], b + at + 0x70, sizeof font_sizes[i]);
                for (unsigned axis = 0; axis < 2; axis++)
                    if (!isfinite(font_sizes[i][axis]) || font_sizes[i][axis] <= 0 || font_sizes[i][axis] > 256) return 0;
                sections[i] = at; text[i] = at + off; seen |= 1u << i;
            }
        }
        at += size;
    }
    if (at != n || seen != 0xfff) return 0;
    /* Caption_00 (font 0 in the pinned layout) contains normal Latin letters.
       Keep each pane's position, colour, animation and world action identifier;
       Latin capitals use 70% of the old icon-cell size to match the adjoining
       action text. Footer menu icons remain untouched. */
    for (unsigned i = 0; i < 12; i++) {
        b[sections[i] + 0x5a] = b[sections[i] + 0x5b] = 0;
        b[text[i]] = (uint8_t)labels[codes[i] - 0xe040]; b[text[i] + 1] = 0;
        font_sizes[i][0] *= 0.70f; font_sizes[i][1] *= 0.70f;
        memcpy(b + sections[i] + 0x70, font_sizes[i], sizeof font_sizes[i]);
    }
    return 12;
}
#include "deck_help_geometry.h"
static int nd_help_text(uint8_t *b, size_t n, const char labels[4]) {
    if(!b || n>128*1024) return 0;
    uint64_t identity=nd_help_hash(b,n);
    if(identity!=0x7b02e47a0bf8c469ull && identity!=0x582ade753eba17c0ull) return 0;
    uint8_t *copy=malloc(n);if(!copy) return 0;memcpy(copy,b,n);
    int okay=1;
    if(identity==0x582ade753eba17c0ull) {
        NdHelpPane panes[512];int count=nd_help_panes(copy,n,panes);
        okay=count>=0 && nd_help_legacy_geometry(copy,panes,count) && nd_help_hash(copy,n)==0x7b02e47a0bf8c469ull;
    }
    int changed=okay && nd_help_geometry_impl(copy,n)==28?nd_help_text_impl(copy,n,labels):0;
    if(changed==12) memcpy(b,copy,n);
    free(copy);return changed;
}
