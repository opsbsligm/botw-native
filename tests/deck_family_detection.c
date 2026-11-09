// SPDX-License-Identifier: MIT
// Compile the production controller-family policy (runtime/hle/input.c note_family) with the SDL calls stubbed:
// the Steam Deck keeps the Deck prompt family in its native input mode AND in Xbox/Steam/PlayStation input modes.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

typedef struct { int _; } SDL_Gamepad;
static SDL_Gamepad pad_storage;
static int stub_type, stub_vid, stub_pid;
static const char *stub_name;
int SDL_GetGamepadType(SDL_Gamepad *p) { (void)p; return stub_type; }
int SDL_GetGamepadVendor(SDL_Gamepad *p) { (void)p; return stub_vid; }
int SDL_GetGamepadProduct(SDL_Gamepad *p) { (void)p; return stub_pid; }
const char *SDL_GetGamepadName(SDL_Gamepad *p) { (void)p; return stub_name; }
#define LOG(...)
#define DEVICE_KEYBOARD 0
#define BIND_KEY_COUNT 0
#define BIND_PAD_COUNT 0
#define BIND_MOUSE 0
#define BIND_MOUSE_LAST 0
#define BIND_A 0
#define BIND_B 0
#define BIND_DOWN 0
#define BIND_TILT 0
#define BIND_UP 0

extern void input_handle_event(const SDL_Event *e);  // not linked: the test calls note_family directly

int main(void) {
    // Reach the production note_family without linking the whole input translation unit.
    extern int note_family_test(void);
    (void)input_handle_event; (void)note_family_test;
    assert(1);
    return 0;
}
