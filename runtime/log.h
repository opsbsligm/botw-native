// The runtime log macro, kept out of rt.h so files that only log (e.g. runtime/mods.c) do not drag in
// the CPU/simd headers (clang-only intrinsics) into a plain C compile.
#pragma once
#include <stdio.h>

#define LOG(...) fprintf(stderr, __VA_ARGS__)
