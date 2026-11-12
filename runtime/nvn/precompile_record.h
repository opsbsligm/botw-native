// SPDX-License-Identifier: MIT
// A complete warm-up record proves that every requested task succeeded.
// Legacy records contain no outcome counts and must be checked again.
#pragma once
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct {
    int shaders, translations, made, cached, failed;
    int pipelines_requested, pipelines_built, pipelines_checked;
    char version[128], date[32];
} SwPrecompileRecord;

static bool sw_precompile_record_complete(const SwPrecompileRecord *r) {
    return r->shaders > 0 && r->translations >= r->shaders && r->made >= 0 && r->cached >= 0 && r->failed == 0 &&
           r->made <= r->translations && r->cached == r->translations - r->made &&
           r->pipelines_requested >= 0 && r->pipelines_built == r->pipelines_requested &&
           r->pipelines_checked == 1;
}
static bool sw_precompile_record_line(FILE *f, char *line, size_t size) {
    if (!fgets(line, (int)size, f)) return false;
    size_t n = strlen(line);
    if (!n || line[n - 1] != '\n') return false;
    line[--n] = 0;
    if (n && line[n - 1] == '\r') line[--n] = 0;
    return n > 0;
}
static bool sw_precompile_record_read(FILE *f, SwPrecompileRecord *r) {
    SwPrecompileRecord out = {0};
    char line[256];
    if (!sw_precompile_record_line(f, line, sizeof line) || strcmp(line, "SWPC2") ||
        !sw_precompile_record_line(f, out.version, sizeof out.version) ||
        !sw_precompile_record_line(f, out.date, sizeof out.date) ||
        !sw_precompile_record_line(f, line, sizeof line)) return false;
    int *values[] = {&out.shaders, &out.translations, &out.made, &out.cached, &out.failed,
                    &out.pipelines_requested, &out.pipelines_built, &out.pipelines_checked};
    char *at = line;
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        char *end;
        errno = 0;
        long value = strtol(at, &end, 10);
        if (errno || end == at || value < 0 || value > INT_MAX ||
            (i != 7 && *end != ' ')) return false;
        *values[i] = (int)value;
        at = end;
        while (*at == ' ') at++;
    }
    if (*at || !sw_precompile_record_line(f, line, sizeof line) || strcmp(line, "END") ||
        fgetc(f) != EOF || ferror(f) || !sw_precompile_record_complete(&out)) return false;
    *r = out;
    return true;
}
static bool sw_precompile_record_write(const char *path, const SwPrecompileRecord *r, unsigned long long nonce) {
    if (!path || !*path || !sw_precompile_record_complete(r) || !r->version[0] || !r->date[0] ||
        !memchr(r->version, 0, sizeof r->version) || !memchr(r->date, 0, sizeof r->date) ||
        strlen(r->version) >= sizeof r->version - 1 || strlen(r->date) >= sizeof r->date - 1 ||
        strpbrk(r->version, "\r\n") || strpbrk(r->date, "\r\n")) return false;
    char pending[1024];
    int n = snprintf(pending, sizeof pending, "%s.tmp-%llu", path, nonce);
    if (n < 0 || n >= (int)sizeof pending) return false;
    FILE *f = fopen(pending, "wx");
    if (!f) return false;
    bool ok = fprintf(f, "SWPC2\n%s\n%s\n%d %d %d %d %d %d %d %d\nEND\n", r->version, r->date,
                      r->shaders, r->translations, r->made, r->cached, r->failed,
                      r->pipelines_requested, r->pipelines_built, r->pipelines_checked) > 0;
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
