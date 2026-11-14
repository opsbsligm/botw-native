#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Little-endian Switch RSTB, owned by the caller. No game data is modified on disk.
bool rstb_budget_valid(const uint8_t *data, size_t size);
// Validate once at load time; lookups/updates require a validated table and retain its invariants.
bool rstb_budget_get(const uint8_t *data, size_t size, const char *path, uint32_t *value);
// Raise an existing entry or insert a missing one, keeping larger mod budgets.
// On failure the input allocation and its contents are unchanged.
bool rstb_budget_raise(uint8_t **data, size_t *size, const char *path, uint32_t floor);
bool rstb_budget_growth(uint32_t original, uint64_t growth, uint32_t *floor);
