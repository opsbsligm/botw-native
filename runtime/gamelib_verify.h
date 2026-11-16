// Host-only inspection API; keep it outside the generated-code ABI headers.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Require this exact file, its sidecar and embedded ID. No guest dispatch.
bool gamelib_verify_file(const char *path, char *why, size_t why_size);
