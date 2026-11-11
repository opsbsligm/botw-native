"""The mods (runtime/mods.c): the RomFS view, the .ips patches on the exefs modules, the rebuild note."""
from pathlib import Path
import struct, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[1]


def nso(build_id=b"\x11" * 32, text=b"\x90" * 64, ro=b"\x41" * 16, data=b"\x00" * 8):
    # a minimal NSO: header, three segments (file/mem/size at 0x10+16*i), bss at 0x3C, the build id at 0x40
    out = bytearray(0x70 + len(text) + len(ro) + len(data))
    out[0:4] = b"NSO0"
    off = 0x70
    segs = []
    for chunk in (text, ro, data):
        segs.append((off, 0x1000000 + off, len(chunk)))
        out[off:off + len(chunk)] = chunk
        off += len(chunk)
    for i, (f, m, s) in enumerate(segs):
        struct.pack_into("<6I", out, 0x10 + 16 * i, f, m, s, 0, 0, 0)
    struct.pack_into("<I", out, 0x3C, 0)
    out[0x40:0x60] = build_id
    return bytes(out)


def ips_bytes(data, offset=0x70):
    return (b"IPS\x1a" + struct.pack(">I", offset)[1:] + struct.pack(">I", len(data))[1:] + data + b"\x00\x00\x00\x00")


C_MAIN = r'''
#include "mods.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char g_root[1024];
const char *rt_data_root(void) { return g_root; }
static int file_exists(const char *p) { struct stat sb; return stat(p, &sb) == 0; }
static void mkdirs_of(char *p) { char *s = p; while ((s = strchr(s, '/'))) { *s = 0; mkdir(p, 0755); *s = '/'; s++; } }
int main(int argc, char **argv) {
    (void)argc;
    const unsigned char ip_bytes[] = {__IPS_BYTES__};
    snprintf(g_root, sizeof g_root, "%s", argv[1]);
    chdir(g_root);  // (rt_paths_init's: the data root is the working directory)
    const char *ext = argv[2];
    // (no mods yet: nothing happens, the user's own overlay env is left)
    setenv("SAVAGEWIND_ROMFS_OVERLAY", "elsewhere", 1);
    assert(mods_romfs_apply() == 0);
    assert(!strcmp(getenv("SAVAGEWIND_ROMFS_OVERLAY"), "elsewhere"));
    assert(!mods_present());
    assert(!mods_need_rebuild(ext));
    // a mod: a romfs file (an add) and an .ips on the exefs main
    char path[2048];
    snprintf(path, sizeof path, "mods/01mod/romfs/Hello.txt");
    mkdirs_of(path);
    FILE *f = fopen(path, "w"); fprintf(f, "mod"); fclose(f);
    assert(mods_romfs_apply() == 1);
    assert(!strcmp(getenv("SAVAGEWIND_ROMFS_OVERLAY"), "extracted/.mods-view"));
    assert(file_exists("extracted/.mods-view/romfs/Hello.txt"));
    // a second mod overrides the first (the link is re-made)
    snprintf(path, sizeof path, "mods/02mod/romfs/Hello.txt");
    mkdirs_of(path);
    f = fopen(path, "w"); fprintf(f, "mod2"); fclose(f);
    assert(mods_romfs_apply() == 2);  // (each mod's file linked; the second's link over the first's)
    char buf[16] = {0};
    f = fopen("extracted/.mods-view/romfs/Hello.txt", "r");
    assert(fread(buf, 1, 15, f) == 4);
    fclose(f);
    assert(!strcmp(buf, "mod2"));
    // the .ips: a record into main's text, a zero keeps the file's byte
    f = fopen("mods/01mod/main.ips", "wb");
    assert(fwrite(ip_bytes, 1, sizeof ip_bytes, f) == sizeof ip_bytes);
    fclose(f);
    char why[512] = "x", rec[2048] = "x";
    int n = mods_exefs_apply(ext, why, sizeof why, rec, sizeof rec);
    assert(!strcmp(why, ""));
    assert(n == 1);
    FILE *m = fopen("extracted/exefs/main", "rb");
    unsigned char got[8];
    fseek(m, 0x70, SEEK_SET);
    assert(fread(got, 1, 8, m) == 8);
    fclose(m);
    assert(got[0] == 1 && got[1] == 2 && got[2] == 0x90 && got[3] == 4);
    assert(mods_need_rebuild(ext));
    mods_mark_built(ext);
    assert(!mods_need_rebuild(ext));
    assert(rec[0]);
    // a mod file changes: the note no longer matches, the start screen offers the rebuild again
    f = fopen("mods/02mod/romfs/Hello.txt", "a"); fputs("!", f); fclose(f);
    assert(mods_need_rebuild(ext));
    // the list (the start screen): both mods, in name order, terminated
    ModsInfo rows[16];
    int nr = mods_list(rows, 16);
    assert(nr == 2 && !strcmp(rows[0].name, "01mod") && !strcmp(rows[1].name, "02mod") && rows[2].name[0] == 0);
    // delete the mods: the view and the env are gone, the note is gone, no rebuild demanded
    system("rm -rf mods");
    assert(!mods_present());
    assert(mods_romfs_apply() == 0);
    assert(!getenv("SAVAGEWIND_ROMFS_OVERLAY"));
    assert(!mods_need_rebuild(ext));
    return 0;
}
'''


class Mods(unittest.TestCase):
    def test_romfs_view_ips_rebuild_note_and_rollback(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as tmp:
            p = Path(tmp)
            data = p / "data"
            ext = data / "extracted"
            exefs = ext / "exefs"
            romfs = ext / "romfs"
            (exefs / "ex").mkdir(parents=True)
            romfs.mkdir()
            (exefs / "main").write_bytes(nso())
            (exefs / "modA").write_bytes(nso(build_id=b"\x22" * 32))
            (romfs / "Hello.txt").write_bytes(b"game")
            (data / "mods").mkdir()

            ips = ips_bytes(bytes([1, 2, 0, 4, 5, 6, 7, 8]))
            c = p / "main.c"
            c.write_text(C_MAIN.replace("__IPS_BYTES__", ",".join(str(b) for b in ips)))
            exe = p / "check"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(ROOT / "runtime"),
                 str(c), str(ROOT / "runtime/mods.c"), "-o", str(exe)],
                check=True, cwd=p)
            subprocess.run([str(exe), str(data), str(ext)], check=True, cwd=p, timeout=10)
