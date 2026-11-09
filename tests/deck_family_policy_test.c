// SPDX-License-Identifier: MIT
// Compile the production controller-family policy from runtime/hle/input.c (note_family + input_pad_family)
// with SDL and the runtime's externs stubbed: the Steam Deck must stay in the Deck prompt family in its
// native input mode AND in Xbox/Steam/PlayStation input modes (where the VID/PID changes and SDL reports
// an Xbox/PS gamepad type, but the controller name still says "Steam Deck").
// The production translation unit needs GNU source (pipe2 / process_vm_*); set it before the
// first system include locks the feature-test set.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <string.h>

#include "../runtime/hle/input.c"  // production translation unit; the stubs below keep the real SDL out of the link

// ---- SDL stubs: the policy under test only reads pad type/vendor/product/name ----
static int stub_type, stub_vid, stub_pid;
static const char *stub_name;
SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *p) { (void)p; return (SDL_GamepadType)stub_type; }
Uint16 SDL_GetGamepadVendor(SDL_Gamepad *p) { (void)p; return (Uint16)stub_vid; }
Uint16 SDL_GetGamepadProduct(SDL_Gamepad *p) { (void)p; return (Uint16)stub_pid; }
const char *SDL_GetGamepadName(SDL_Gamepad *p) { (void)p; return stub_name; }
void SDL_free(void *p) { (void)p; }
void SDL_CloseGamepad(SDL_Gamepad *p) { (void)p; }
int16_t SDL_GetGamepadAxis(SDL_Gamepad *p, SDL_GamepadAxis a) { (void)p; (void)a; return 0; }
bool SDL_GetGamepadButton(SDL_Gamepad *p, SDL_GamepadButton b) { (void)p; (void)b; return false; }
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad *p) { (void)p; return 0; }
SDL_JoystickID *SDL_GetGamepads(int *n) { *n = 0; (void)0; return 0; }
SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode sc, SDL_Keymod m, bool ke) { (void)sc; (void)m; (void)ke; return 0; }
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode k, SDL_Keymod *m) { (void)k; (void)m; return 0; }
SDL_Scancode SDL_GetScancodeFromName(const char *n) { (void)n; return 0; }
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { (void)id; return 0; }
bool SDL_PushEvent(SDL_Event *e) { (void)e; return true; }
bool SDL_SetWindowRelativeMouseMode(SDL_Window *w, bool on) { (void)w; (void)on; return true; }

// ---- extern stubs: only what the production translation unit links against ----
bool menu_input_captured(void) { return false; }
uint32_t menu_suppressed_pad_buttons(void) { return 0; }
bool menu_cursor_free(void) { return false; }
void motion_tilt_held(bool on) { (void)on; }
void motion_pad_opened(SDL_Gamepad *p) { (void)p; }
void motion_pad_closed(void) {}
void motion_sensor_event(const SDL_Event *e) { (void)e; }
void motion_mouse(float dx, float dy) { (void)dx; (void)dy; }
void vibration_update(SDL_Gamepad *p, bool on) { (void)p; (void)on; }
int16_t g_bind_key[BIND_KEY_COUNT];
int16_t g_bind_pad[BIND_PAD_COUNT];
Settings g_settings;
void watch_arm(void) {}
void watch_disarm(void) {}
KEvent *kernel_create_event_pair(uint32_t *w, uint32_t *r) { (void)w; (void)r; return 0; }
void kernel_signal(KEvent *ev) { (void)ev; }
void klock(void) {}
pthread_mutex_t g_klock = PTHREAD_MUTEX_INITIALIZER;
void guest_call(Cpu *c, uint64_t addr) { (void)c; (void)addr; }
uint64_t guest_sym(const char *sym) { (void)sym; return 0; }
bool rt_resource(const char *name, const char *dev_rel, char *out, size_t n) { (void)name; (void)dev_rel; (void)out; (void)n; return false; }
void bind_capture_begin(int dev, int slot) { (void)dev; (void)slot; }
bool bind_capture_event(const SDL_Event *e) { (void)e; return false; }
void bind_reset(int dev) { (void)dev; }

// ---- remaining externs the full translation unit references ----
uint64_t g_watch_lo, g_watch_hi;
Cpu *kernel_current_cpu(void) { return 0; }
uint64_t plat_now_ns(void) { return 0; }
uint64_t renderer_present_count(void) { return 0; }
void motion_script(float a, float b, float c, float d) { (void)a; (void)b; (void)c; (void)d; }
int bind_pad_from_name(const char *name) { (void)name; return -1; }
int bind_slot_from_name(const char *name) { (void)name; return -1; }

// ---- __linux__ block: the menu-confirm path the production unit calls (see gamepad_buttons_now) ----
bool gamelib_legacy_sdk7_profile(void) { return true; }
bool nd_botw_menu_context(NdMenuRead read, void *user) { (void)read; (void)user; return false; }
uint64_t nd_menu_confirm(NdMenuConfirm *st, bool active, unsigned held,
                         uint64_t east_actions, uint64_t south_actions) {
    (void)st; (void)active; (void)east_actions; (void)south_actions; return held;
}
static char fake_pad_storage[256];
static void set_pad(int type, int vid, int pid, const char *name) {
    stub_type = type; stub_vid = vid; stub_pid = pid; stub_name = name;
    pad = (SDL_Gamepad *)fake_pad_storage;  // production note_family (same translation unit) against the stubbed pad
    note_family();
}

int main(void) {
    // Native Deck input mode: VID 0x28de / PID 0x1205.
    set_pad(SDL_GAMEPAD_TYPE_XBOXONE, 0x28de, 0x1205, "Steam Deck Controller");
    assert(input_pad_family() == 3);

    // Xbox input mode: SDL reports a different product id, but the name still identifies the Deck.
    set_pad(SDL_GAMEPAD_TYPE_XBOXONE, 0x1430, 0x0a0b, "Steam Deck Controller");
    assert(input_pad_family() == 3);

    // PlayStation input mode (PS4 gamepad type): the name match must win over the type.
    set_pad(SDL_GAMEPAD_TYPE_PS4, 0x1430, 0x0a08, "Steam Deck Controller");
    assert(input_pad_family() == 3);

    // A genuine Xbox controller keeps the Xbox family.
    set_pad(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b05, "Xbox One Gamepad");
    assert(input_pad_family() == 1);

    // A genuine PlayStation controller keeps the PlayStation family.
    set_pad(SDL_GAMEPAD_TYPE_PS5, 0x11c0, 0x1f4b, "DualSense Wireless Controller");
    assert(input_pad_family() == 2);

    // A controller SDL does not classify (non-Xbox/PS type, unknown VID/PID, no Deck name) stays Switch/other.
    set_pad(SDL_GAMEPAD_TYPE_GAMECUBE, 0x05fe, 0x3009, "HORI Fighting Commander");
    assert(input_pad_family() == 0);

    // No pad at all: still Switch/other.
    set_pad(0, 0, 0, "HORI Fighting Commander");
    assert(input_pad_family() == 0);
    return 0;
}
