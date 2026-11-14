// Run the actual fs.c RSTB loader/merger against synthetic original and mod files.
#define _POSIX_C_SOURCE 200809L
#include "rstb_budget.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define FATAL(...) do { fprintf(stderr, "FATAL: " __VA_ARGS__); exit(7); } while (0)
typedef struct { const uint8_t *data; size_t size; } RomfsImage;
typedef struct { uint64_t start, size; char *path; int fd; const uint8_t *mem; } Overlay;
typedef struct { uint64_t off, size; char *path; uint32_t meta; } RomFile;
typedef struct { const char *path; uint64_t grow; } GrownFile;
typedef struct {
    RomfsImage *img;
    RomFile *rom_files;
    int rom_nfiles, ngrown;
    GrownFile *grown;
    uint8_t *rstb_blob, *ov_meta;
    uint64_t ov_end, rom_data_off;
    Overlay *ov;
    int ov_n, ov_cap;
} RomImage;
static RomImage images[1], *R = &images[0];
static bool romfs_has_dlc_merged(void) { return getenv("TEST_MERGED_DLC") != NULL; }
static bool overlay_file(const char *path, char *host, size_t size) {
    (void)path;
    const char *overlay = getenv("TEST_RSTB_OVERLAY");
    if (!overlay) return false;
    assert(snprintf(host, size, "%s", overlay) < (int)size);
    return true;
}
static ssize_t romfs_image_pread(RomfsImage *img, void *out, size_t size, uint64_t off) {
    if (off > img->size || size > img->size - off) return -1;
    memcpy(out, img->data + off, size);
    return (ssize_t)size;
}
static void meta_copy(const uint8_t *meta, uint64_t off, uint64_t size) {
    (void)off;
    if (!R->ov_meta) { R->ov_meta = malloc(size); assert(R->ov_meta); memcpy(R->ov_meta, meta, size); }
}
#include "rstb_fs_under_test.inc"

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); long n = ftell(f); assert(n >= 0);
    rewind(f); uint8_t *data = malloc((size_t)n + 1); assert(data);
    assert(fread(data, 1, (size_t)n, f) == (size_t)n); fclose(f);
    *size = (size_t)n; return data;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    size_t size;
    uint8_t *data = read_file(argv[1], &size);
    if (!strcmp(argv[2], "validate")) {
        bool valid = rstb_budget_valid(data, size); free(data); return valid ? 0 : 3;
    }
    if (!strcmp(argv[2], "arithmetic")) {
        uint32_t floor = 123;
        assert(rstb_budget_growth(32710368, 18036, &floor) && floor == 32728448);
        assert(rstb_budget_growth(0, 0, &floor) && floor == 32);
        assert(!rstb_budget_growth(UINT32_MAX, 0, &floor));
        assert(!rstb_budget_growth(1, UINT64_MAX, &floor));
        assert(!rstb_budget_growth(0, UINT32_MAX, &floor));
        free(data); return 0;
    }
    RomfsImage image = {data, size};
    RomFile files[] = {{0, size, "System/Resource/ResourceSizeTable.product.srsizetable", 0},
                       {0, 0, "Actor/Test.sbyml", 32}};
    GrownFile growth[] = {{"Layout/Common.blarc", 18036}, {"Voice/stream.bfstm", 128}};
    R->img = &image; R->rom_files = files; R->rom_nfiles = 2;
    R->grown = growth; R->ngrown = 2; R->ov_end = 0x1000;
    uint8_t meta[64] = {0};
    rstb_patch(meta, 0, sizeof meta);
    assert(R->ov_n == 1 && R->rstb_blob == R->ov[0].mem);
    // Verify file-meta publication, not just a helper's return value.
    uint64_t published_size, published_offset;
    memcpy(&published_offset, R->ov_meta + 8, 8);
    memcpy(&published_size, R->ov_meta + 16, 8);
    assert(published_offset == R->ov[0].start && published_size == R->ov[0].size);
    size_t out_size;
    uint8_t *out = yaz0_unpack(R->rstb_blob, R->ov[0].size, &out_size);
    assert(out && fwrite(out, 1, out_size, stdout) == out_size);
    free(out); free(R->ov); free(R->ov_meta); free(R->rstb_blob); free(data);
    return 0;
}
