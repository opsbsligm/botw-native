// Mods (runtime/mods.h). The player's own content for this game copy, in <data root>/mods.
#include "mods.h"

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "paths.h"
#include "log.h"
#include "ips_patch.h"

// Every snprintf here joins a bounded dir and a <=255-byte name into a PATH_MAX buffer; when the join would
// overflow, the caller stats/unlinks the result and treats a short path as "not there". GCC's -Wformat-truncation
// flags these joins as "may be truncated" under -Werror even though the truncation is always handled; clang does
// not emit the warning, so scope the suppression to the GCC warning group (unknown to clang, which ignores it).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

// ------------------------------------------------------------------ the mod folders

static const char *g_view;  // <extracted>/.mods-view, while the overlay reads it

typedef struct DirEntry {
    char name[256];
} DirEntry;

static int list_dir(const char *dir, DirEntry *out, int cap) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (e->d_name[0] == '.') continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
        n++;
    }
    closedir(d);
    int i, j;
    for (i = 0; i + 1 < n; i++)  // (name order: later mods override earlier ones)
        for (j = i + 1; j < n; j++)
            if (strcmp(out[j].name, out[i].name) < 0) {
                DirEntry t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
    return n;
}

static bool is_dir(const char *p) {
    struct stat sb;
    return stat(p, &sb) == 0 && S_ISDIR(sb.st_mode);
}

static void remove_tree(const char *p) {
    if (!p || !*p) return;
    DIR *d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char c[PATH_MAX];
            snprintf(c, sizeof c, "%s/%s", p, e->d_name);
            remove_tree(c);
        }
        closedir(d);
    }
    rmdir(p);
}

const char *mods_dir(void) {
    static char out[1024];
    const char *root = rt_data_root();
    if (!root || !*root) return "mods";  // (rt_paths_init makes the data root the working directory)
    snprintf(out, sizeof out, "%s/mods", root);
    return out;
}

bool mods_present(void) {
    const char *mods = mods_dir();
    if (!is_dir(mods)) return false;
    DIR *d = opendir(mods);
    if (!d) return false;
    struct dirent *e;
    bool any = false;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') {
            any = true;
            break;
        }
    closedir(d);
    return any;
}

// ------------------------------------------------------------------ the RomFS mods: one merged view for the overlay

static void walk_stage(const char *dir, const char *rel, const char *view, int *nfiles) {
    struct dirent *e;
    DIR *d = opendir(dir);
    if (!d) return;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[PATH_MAX], rel2[PATH_MAX], lnk[PATH_MAX], dd[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        snprintf(rel2, sizeof rel2, "%s%s%s", rel ? rel : "", rel ? "/" : "", e->d_name);
        struct stat sb;
        if (stat(full, &sb)) continue;
        if (S_ISDIR(sb.st_mode)) {
            snprintf(dd, sizeof dd, "%s/%s", view, rel2);
            mkdir(dd, 0755);  // (it can already exist: from an earlier mod)
            walk_stage(full, rel2, view, nfiles);
            continue;
        }
        if (!S_ISREG(sb.st_mode)) continue;
        snprintf(lnk, sizeof lnk, "%s/%s", view, rel2);
        {   // the file's own directory in the view
            char parent[PATH_MAX];
            snprintf(parent, sizeof parent, "%s", lnk);
            char *sl = strrchr(parent, '/');
            if (sl > parent) {
                *sl = 0;
                mkdir(parent, 0755);  // (it can already exist: from an earlier mod)
            }
        }
        if (unlink(lnk) < 0 && errno != ENOENT) continue;  // (an earlier mod's link)
        if (link(full, lnk) < 0) {  // (same volume: the data root on both sides)
            LOG("[mods] %s: %s\n", rel2, strerror(errno));
            continue;
        }
        (*nfiles)++;
    }
    closedir(d);
}

int mods_romfs_apply(void) {
    static char mods[1024], view[1024];
    snprintf(mods, sizeof mods, "%s", mods_dir());
    snprintf(view, sizeof view, "%s", "extracted/.mods-view");
    int nfiles = 0;
    if (is_dir(mods)) {
        DirEntry entry[64];
        int nmod = list_dir(mods, entry, 64);
        if (nmod) {
            remove_tree(view);
            mkdir(view, 0755);
            for (int i = 0; i < nmod; i++) {
                char root[1024];
                snprintf(root, sizeof root, "%s/%s", mods, entry[i].name);
                if (!is_dir(root)) continue;
                LOG("[mods] romfs %s\n", entry[i].name);
                walk_stage(root, NULL, view, &nfiles);
            }
        }
    }
    if (nfiles) {
        setenv("SAVAGEWIND_ROMFS_OVERLAY", view, 1);
        g_view = view;
        return nfiles;
    }
    if (g_view || getenv("SAVAGEWIND_ROMFS_OVERLAY")) {  // (none this boot: drop ours, leave a user's)
        if (g_view) {
            remove_tree(g_view);
            g_view = NULL;
        }
        const char *env = getenv("SAVAGEWIND_ROMFS_OVERLAY");
        if (env && !strncmp(env, "extracted/.mods-view", strlen("extracted/.mods-view")) &&
            (env[strlen("extracted/.mods-view")] == 0 || env[strlen("extracted/.mods-view")] == '/'))
            unsetenv("SAVAGEWIND_ROMFS_OVERLAY");
    }
    return 0;
}

// ------------------------------------------------------------------ SHA-256 (the mods' fingerprint; the stdlib has no portable one)

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    uint32_t buflen;
} Sha256;

static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(Sha256 *s, const uint8_t *p) {
    uint32_t w[64], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha_k[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha256_update(Sha256 *s, const uint8_t *p, size_t n) {
    s->len += n;
    while (n) {
        uint32_t k = 64 - s->buflen;
        if (k > n) k = (uint32_t)n;
        memcpy(s->buf + s->buflen, p, k);
        s->buflen += k;
        p += k;
        n -= k;
        if (s->buflen == 64) {
            sha256_block(s, s->buf);
            s->buflen = 0;
        }
    }
}

static void sha256_begin(Sha256 *s) {
    memset(s->h, 0, sizeof s->h);
    s->h[0] = 0x6a09e667; s->h[1] = 0xbb67ae85; s->h[2] = 0x3c6ef372; s->h[3] = 0xa54ff53a;
    s->h[4] = 0x510e527f; s->h[5] = 0x9b05f88c; s->h[6] = 0x1f83d9ab; s->h[7] = 0x5be0cd19;
    s->len = 0;
    s->buflen = 0;
}

static void sha256_hex(const Sha256 *s, char *out65) {
    Sha256 t = *s;
    uint8_t pad = 0x80;
    sha256_update(&t, &pad, 1);
    uint8_t z = 0;
    while (t.buflen != 56) sha256_update(&t, &z, 1);
    uint8_t lenb[8];
    uint64_t bits = t.len * 8;
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(&t, lenb, 8);
    for (int i = 0; i < 8; i++) sprintf(out65 + 8 * i, "%08x", t.h[i]);
    out65[64] = 0;
}

// ------------------------------------------------------------------ the ExeFS patches (.ips on the exefs modules)

static uint8_t *read_whole(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*len ? *len : 1);
    if (p && fread(p, 1, *len, f) != *len) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return p;
}

// The NSO's sections: file/memory/decoded size at 0x10/0x14/0x18+16*i; stored compressed sizes at 0x60+4*i.
// Fingerprints hash stored bytes, never decoded lengths into a compressed file.
// and its build-id at 0x40 (what runtime/loader.c checks against the layout).
static bool nso_sections(const uint8_t *raw, size_t len, uint32_t seg[3][3], uint32_t *bss) {
    if (len < 0x100 || memcmp(raw, "NSO0", 4)) return false;
    uint32_t flags; memcpy(&flags, raw + 0x0C, 4);
    for (int i = 0; i < 3; i++) {
        memcpy(&seg[i][0], raw + 0x10 + i * 0x10, 4);
        memcpy(&seg[i][1], raw + 0x14 + i * 0x10, 4);
        memcpy(&seg[i][2], raw + ((flags & (1u << i)) ? 0x60 + i * 4 : 0x18 + i * 0x10), 4);
        if (seg[i][0] > len || seg[i][2] > len - seg[i][0] || (seg[i][2] && seg[i][0] < 0x100)) return false;
    }
    memcpy(bss, raw + 0x3C, 4);
    return true;
}

static bool nso_build_id(const uint8_t *raw, size_t len, char *id65) {
    if (len < 0x60) return false;
    for (int i = 0; i < 32; i++) sprintf(id65 + 2 * i, "%02x", raw[0x40 + i]);
    return true;
}

// Standard IPS/IPS32 writes every byte. The older private IPS\x1a dialect retains its zero-means-keep convention.
// Returns the number of records applied, -1 a problem (the message in <why>).
static int apply_ips(const char *ips_path, const char *module, uint8_t *raw, size_t len, char *why, size_t why_size) {
    size_t n;
    uint8_t *ip = read_whole(ips_path, &n);
    if (ip && n >= 5 && (!memcmp(ip, "PATCH", 5) || !memcmp(ip, "IPS32", 5))) {
        uint32_t flags = 0;
        if (len >= 0x100) memcpy(&flags, raw + 0x0C, sizeof flags);
        if (len < 0x100 || memcmp(raw, "NSO0", 4) || (flags & 7)) {
            if (why && why_size) snprintf(why, why_size, "%s: standard IPS requires an uncompressed NSO with a full header", module);
            free(ip); return -1;
        }
        char detail[128];
        int result = sw_ips_apply(ip, n, raw, len, 0x100, detail, sizeof detail);
        if (result < 0 && why && why_size) snprintf(why, why_size, "%s: %s: %s", module, ips_path, detail);
        free(ip); return result;
    }
    if (!ip || n < 4 || memcmp(ip, "IPS\x1a", 4)) {
        if (why && why_size) snprintf(why, why_size, "%s: %s is not an IPS", module, ips_path);
        free(ip);
        return -1;
    }
    size_t o = 4;
    int count = 0;
    while (o + 3 <= n) {
        uint32_t off = (uint32_t)ip[o] << 16 | (uint32_t)ip[o + 1] << 8 | ip[o + 2];
        o += 3;
        if (!off) break;  // the trailing 0x00000000 (with its optional checksum)
        if (o + 3 > n) {
            if (why && why_size) snprintf(why, why_size, "%s: %s is cut off", module, ips_path);
            free(ip);
            return -1;
        }
        uint32_t sz = (uint32_t)ip[o] << 16 | (uint32_t)ip[o + 1] << 8 | ip[o + 2];
        o += 3;
        if (sz == 0 || o + sz > n) {
            if (why && why_size) snprintf(why, why_size, "%s: %s has a record of %u bytes past its end", module, ips_path, sz);
            free(ip);
            return -1;
        }
        if ((uint64_t)off + sz > len) {
            if (why && why_size) snprintf(why, why_size, "%s: %s reaches past the %s module (0x%x past 0x%zx)", module,
                                          ips_path, module, off + sz, len);
            free(ip);
            return -1;
        }
        for (uint32_t i = 0; i < sz; i++)
            if (ip[o + i]) raw[off + i] = ip[o + i];
        o += sz;
        count++;
    }
    free(ip);
    return count;
}

// A mod's files, each by its path (in order): what the RomFS overlay and the start screen show.
static void walk_count(const char *dir, const char *rel, int *nfiles, int *nips) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[PATH_MAX], rel2[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        snprintf(rel2, sizeof rel2, "%s%s%s", rel ? rel : "", rel ? "/" : "", e->d_name);
        struct stat sb;
        if (stat(full, &sb)) continue;
        if (S_ISDIR(sb.st_mode)) {
            walk_count(full, rel2, nfiles, nips);
            continue;
        }
        if (!S_ISREG(sb.st_mode)) continue;
        (*nfiles)++;
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".ips")) (*nips)++;
    }
    closedir(d);
}

// A mod's files (name + content, in order) and the exefs modules' code sections (text and ro: what the game code
// was built from).
static void walk_hash(const char *dir, const char *rel, Sha256 *s) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[PATH_MAX], rel2[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        snprintf(rel2, sizeof rel2, "%s%s%s", rel ? rel : "", rel ? "/" : "", e->d_name);
        struct stat sb;
        if (stat(full, &sb)) continue;
        if (S_ISDIR(sb.st_mode)) {
            walk_hash(full, rel2, s);
            continue;
        }
        if (!S_ISREG(sb.st_mode)) continue;
        size_t len;
        uint8_t *raw = read_whole(full, &len);
        sha256_update(s, (const uint8_t *)rel2, strlen(rel2));
        sha256_update(s, (const uint8_t *)"\n", 1);
        if (raw) {
            sha256_update(s, raw, len);
            free(raw);
        }
    }
    closedir(d);
}

// The fingerprint the start screen keeps: the mods (every file, name + content) and the exefs code sections.
static void mods_fingerprint(const char *extracted, char *out65) {
    Sha256 s;
    sha256_begin(&s);
    const char *mods = mods_dir();
    if (is_dir(mods)) {
        DirEntry entry[64];
        int nmod = list_dir(mods, entry, 64);
        for (int i = 0; i < nmod; i++) {
            char root[1024];
            snprintf(root, sizeof root, "%s/%s", mods, entry[i].name);
            if (!is_dir(root)) continue;
            sha256_update(&s, (const uint8_t *)entry[i].name, strlen(entry[i].name));
            sha256_update(&s, (const uint8_t *)"\n", 1);
            walk_hash(root, "", &s);
            sha256_update(&s, (const uint8_t *)"\n", 1);
        }
    }
    char exefs[1024];
    snprintf(exefs, sizeof exefs, "%s/exefs", extracted);
    if (is_dir(exefs)) {
        DirEntry mod[64];
        int nmod = list_dir(exefs, mod, 64);
        for (int i = 0; i < nmod; i++) {
            char p[1024];
            size_t len;
            uint32_t seg[3][3], bss;
            snprintf(p, sizeof p, "%s/%s", exefs, mod[i].name);
            uint8_t *raw = read_whole(p, &len);
            if (!raw) continue;
            if (nso_sections(raw, len, seg, &bss)) {
                sha256_update(&s, (const uint8_t *)mod[i].name, strlen(mod[i].name));
                sha256_update(&s, (const uint8_t *)"\t", 1);
                sha256_update(&s, raw + seg[0][0], seg[0][2]);  // text
                sha256_update(&s, (const uint8_t *)"\t", 1);
                sha256_update(&s, raw + seg[1][0], seg[1][2]);  // ro
                sha256_update(&s, (const uint8_t *)"\n", 1);
            }
            free(raw);
        }
    }
    sha256_hex(&s, out65);
}

// The note the setup writes where the patches landed (what the start screen shows), "<module> x<records>".
static void record_path(const char *extracted, char *out, size_t n) {
    snprintf(out, n, "%s/.mods-patched", extracted);
}

static bool mods_write_atomic(const char *path, const void *bytes, size_t length) {
    static _Atomic unsigned serial;
    char pending[1400];
    int n = snprintf(pending, sizeof pending, "%s.mods-%ld-%u.tmp", path, (long)getpid(), atomic_fetch_add(&serial, 1));
    if (n < 0 || n >= (int)sizeof pending) return false;
    FILE *f = fopen(pending, "wbx");
    if (!f) return false;
    bool ok = !length || fwrite(bytes, 1, length, f) == length;
    if (fflush(f)) ok = false;
#ifdef _WIN32
    if (_commit(_fileno(f))) ok = false;
#else
    if (fsync(fileno(f))) ok = false;
#endif
    if (fclose(f)) ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(pending, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(pending, path) == 0;
#endif
    }
    if (!ok) remove(pending);
    return ok;
}

int mods_exefs_apply(const char *extracted, char *why, size_t why_size, char *record, size_t record_size) {
    if (why && why_size) why[0] = 0;
    if (record && record_size) record[0] = 0;
    static char mods[1024];
    snprintf(mods, sizeof mods, "%s", mods_dir());
    char rec[1024];
    record_path(extracted, rec, sizeof rec);
    if (!mods_present()) {
        if (remove(rec) < 0 && errno != ENOENT) {
            if (why && why_size) snprintf(why, why_size, "%s: %s", rec, strerror(errno));
        }
        if (record && record_size) snprintf(record, record_size, "%s", "");
        return 0;
    }
    DirEntry entry[64];
    int nmod = list_dir(mods, entry, 64);
    char *patched = NULL;  // the note's lines so far: "<module> x<records>\n"
    size_t plen = 0;
    for (int i = 0; i < nmod; i++) {
        char root[1024];
        snprintf(root, sizeof root, "%s/%s", mods, entry[i].name);
        if (!is_dir(root)) continue;
        DirEntry ips[128];
        int nips = list_dir(root, ips, 128);
        for (int k = 0; k < nips; k++) {
            size_t l = strlen(ips[k].name);
            if (l < 5 || strcmp(ips[k].name + l - 4, ".ips")) continue;
            char modname[256];  // main.ips -> main (the exefs module it patches)
            snprintf(modname, sizeof modname, "%.*s", (int)(l - 4), ips[k].name);
            char mp[1024];
            snprintf(mp, sizeof mp, "%s/exefs/%s", extracted, modname);
            size_t len;
            uint8_t *raw = read_whole(mp, &len);
            if (!raw) {
                if (why && why_size) snprintf(why, why_size, "%s: the %s module isn't in the game's exefs",
                                              entry[i].name, modname);
                goto failed;
            }
            uint32_t seg[3][3], bss;
            if (!nso_sections(raw, len, seg, &bss)) {
                if (why && why_size) snprintf(why, why_size, "%s: %s isn't an NSO module", entry[i].name, modname);
                free(raw);
                goto failed;
            }
            char id_before[65], id_after[65];
            nso_build_id(raw, len, id_before);
            char ipsp[1024];
            snprintf(ipsp, sizeof ipsp, "%s/%s", root, ips[k].name);
            char pw[512];
            int c = apply_ips(ipsp, modname, raw, len, pw, sizeof pw);
            if (c < 0) {
                if (why && why_size) snprintf(why, why_size, "%s: %s", entry[i].name, pw);
                free(raw);
                goto failed;
            }
            if (!c) {
                free(raw);
                continue;
            }
            nso_build_id(raw, len, id_after);
            if (strcmp(id_before, id_after)) {
                if (why && why_size) snprintf(why, why_size, "%s: %s changed the module's build id", entry[i].name,
                                              ips[k].name);
                free(raw);
                goto failed;
            }
            if (!mods_write_atomic(mp, raw, len)) {
                if (why && why_size) snprintf(why, why_size, "%s: %s couldn't be written", entry[i].name, modname);
                free(raw);
                goto failed;
            }
            free(raw);
            char *next = realloc(patched, plen + 256);
            if (!next) {
                if (why && why_size) snprintf(why, why_size, "Not enough memory to record applied patches");
                goto failed;
            }
            patched = next;
            plen += (size_t)snprintf(patched + plen, 256, "%s x%d\n", modname, c);
            LOG("[mods] exefs %s: %s, %d record%s\n", entry[i].name, ips[k].name, c, c > 1 ? "s" : "");
        }
    }
    // the note (what was applied, once): no fingerprint yet - the game code is not built with this, still (that
    // is what mods_mark_built writes, the fingerprint of the exefs as it stands now)
    if (!mods_write_atomic(rec, patched, plen)) {
        if (why && why_size) snprintf(why, why_size, "Could not publish the applied-patches record");
        goto failed;
    }
    free(patched);
    if (record && record_size) snprintf(record, record_size, "%s", rec);
    if (why && why_size) why[0] = 0;
    // the number of modules left patched (the note's lines)
    int n = 0;
    FILE *f = fopen(rec, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f))
            if (line[0] != 'f') n++;
        fclose(f);
    }
    return n;
failed:
    free(patched);
    remove(rec);  // Never preserve a previous successful rebuild claim after a rejected patch.
    return -1;
}

// The note read back: "" when there is none or nothing in it.
static void read_record(const char *extracted, char *out, size_t out_size, char *fingerprint65) {
    char rec[1024];
    record_path(extracted, rec, sizeof rec);
    if (out) out[0] = 0;
    if (fingerprint65) fingerprint65[0] = 0;
    FILE *f = fopen(rec, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        if (l && line[l - 1] == '\n') line[--l] = 0;
        if (!strncmp(line, "fingerprint ", 12) && strlen(line) >= 12 + 64) {
            if (fingerprint65) snprintf(fingerprint65, 65, "%s", line + 12);
            continue;
        }
        if (!out) continue;
        size_t have = strlen(out);
        if (have) {
            if (have + l + 2 >= out_size) break;
            strncat(out, "\n", out_size - have - 1);
        }
        if (have + l + 1 >= out_size) break;
        strncat(out, line, out_size - have - 1);
    }
    fclose(f);
}

bool mods_need_rebuild(const char *extracted) {
    if (!mods_present()) return false;
    char cur[65], have[65];
    read_record(extracted, NULL, 0, have);
    mods_fingerprint(extracted, cur);
    return strcmp(have, cur) != 0;  // (no note yet, or the mods / the exefs changed since the build)
}

void mods_mark_built(const char *extracted) {
    if (!mods_present()) return;
    static char rec[1024];
    record_path(extracted, rec, sizeof rec);
    char fp[65];
    mods_fingerprint(extracted, fp);
    // rewrite the note with the fresh fingerprint (the applied lines stay)
    char body[4096];
    read_record(extracted, body, sizeof body, NULL);
    FILE *f = fopen(rec, "w");
    if (f) {
        if (body[0]) fprintf(f, "%s\n", body);
        fprintf(f, "fingerprint %s\n", fp);
        fclose(f);
    }
}

// ------------------------------------------------------------------ the start screen's list of the mods

int mods_list(ModsInfo *rows, int cap) {
    static char mods[1024];
    snprintf(mods, sizeof mods, "%s", mods_dir());
    DirEntry entry[64];
    int nmod = is_dir(mods) ? list_dir(mods, entry, 64) : 0;
    static char detail[64][160];
    int n = 0;
    for (int i = 0; i < nmod && n < cap; i++) {
        char root[1024];
        snprintf(root, sizeof root, "%s/%s", mods, entry[i].name);
        if (!is_dir(root)) continue;
        int nfiles = 0, nips = 0;
        walk_count(root, "", &nfiles, &nips);
        if (nfiles)
            snprintf(detail[n], sizeof detail[n], "%d file%s, %d module patch%s", nfiles, nfiles == 1 ? "" : "s",
                     nips, nips == 1 ? "" : "es");
        else snprintf(detail[n], sizeof detail[n], "no files");
        rows[n].name = entry[i].name;
        rows[n].detail = detail[n];
        n++;
    }
    if (n < cap) rows[n].name = rows[n].detail = "";
    return n;
}

#pragma GCC diagnostic pop
