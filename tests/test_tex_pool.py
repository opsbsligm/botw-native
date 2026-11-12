"""CA4 HD texture packs: the settings round-trip and the pool cap the host warns on (settings_tex_pool_cap)."""
import os, shutil, subprocess, tempfile, unittest
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MAIN = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "language.h"

// (the externals settings.c pulls in, stubbed so it links on its own)
void bind_settings_load(const char *k, const char *v) { (void)k; (void)v; }
void bind_settings_save(FILE *f) { (void)f; }
bool fsr3_upscaler_enabled(void) { return false; }
const TextLanguage g_text_languages[1] = {{"Auto", "en-US", NULL}};
const int g_ntext_languages = 1;
const VoiceLanguage g_voice_languages[1] = {{"Text", NULL}};
const int g_nvoice_languages = 1;

int main(int argc, char **argv) {
    (void)argc;
    setenv("SAVAGEWIND_SETTINGS", argv[1], 1);
    // 1. a cap in the file: load it, and the bytes follow
    {
        FILE *f = fopen(argv[1], "w");
        fprintf(f, "tex_pool_mib = 4096\n");
        fclose(f);
    }
    settings_load();
    assert(g_settings.tex_pool_mib == 4096);
    assert(settings_tex_pool_cap() == (uint64_t)4096 << 20);
    // 2. the env override wins for a one-off run; 0 disables the cap
    setenv("SAVAGEWIND_MAX_TEXTURE_POOL", "512", 1);
    assert(settings_tex_pool_cap() == (uint64_t)512 << 20);
    setenv("SAVAGEWIND_MAX_TEXTURE_POOL", "0", 1);
    assert(settings_tex_pool_cap() == 0);
    unsetenv("SAVAGEWIND_MAX_TEXTURE_POOL");
    assert(settings_tex_pool_cap() == (uint64_t)4096 << 20);
    // 3. saving and reading the file back keeps the value
    settings_save();
    FILE *f = fopen(argv[1], "r");
    char buf[16384] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    (void)n;
    fclose(f);
    assert(strstr(buf, "tex_pool_mib = 4096") != NULL);
    // 4. a file without the key leaves no cap (the guest's own pool size stands)
    g_settings.tex_pool_mib = 0;
    { char rmcmd[2048]; snprintf(rmcmd, sizeof rmcmd, "rm %s", argv[1]); system(rmcmd); }
    settings_save();
    f = fopen(argv[1], "r");
    memset(buf, 0, sizeof buf);
    n = fread(buf, 1, sizeof buf - 1, f);
    (void)n;
    fclose(f);
    assert(strstr(buf, "tex_pool_mib") == NULL);
    assert(settings_tex_pool_cap() == 0);
    return 0;
}
'''


class TexPool(unittest.TestCase):
    def test_roundtrip_and_env_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = os.path.join(tmp, "savagewind.ini")
            main = os.path.join(tmp, "main.c")
            with open(main, "w") as f:
                f.write(MAIN)
            exe = os.path.join(tmp, "check")
            # Match the real build's C flags for x86-64 (Makefile RTFLAGS + GENARCH): gnu
            # default std, no -Werror, -fno-strict-aliasing, and -march=x86-64-v3 on x86_64
            # hosts (the simd.h FMA / BMI2 paths need it). arm64 hosts build without a
            # march flag, as the macOS / arm64 Linux native path does.
            # The project builds with clang (Makefile CC := clang); prefer it here and fall
            # back to the default C compiler. RTFLAGS as-is (gnu default std, no -Werror,
            # no -march): the simd headers compile clean under this flag set on the CI
            # x86-64 container and on macOS alike. -lm for lroundf on Linux.
            cc = shutil.which("clang") or "cc"
            flags = [cc, "-O2", "-g", "-fno-strict-aliasing", "-Wall", "-Wno-unused-function",
                     "-I", os.path.join(ROOT, "runtime"),
                     "-I", os.path.join(ROOT, "runtime", "nvn"),
                     "-isystem", os.path.join(ROOT, "third_party"),
                     "-isystem", os.path.join(ROOT, "third_party", "stb")]
            subprocess.run(flags + [main, os.path.join(ROOT, "runtime", "settings.c"), "-lm", "-o", exe],
                           check=True, cwd=tmp)
            subprocess.run([exe, ini], check=True, cwd=tmp, timeout=10)


if __name__ == "__main__":
    unittest.main()
