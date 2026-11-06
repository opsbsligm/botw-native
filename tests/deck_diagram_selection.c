// SPDX-License-Identifier: MIT
// Compile production glyph selection, not a copy of the device policy.
#include "../runtime/nvn/glyphs.c"
#include <assert.h>
Settings g_settings={0};
int16_t g_bind_key[BIND_KEY_COUNT]={0},g_bind_pad[BIND_PAD_COUNT]={0};
static int family,device;
int input_pad_family(void) {return family;}
int input_last_device(void) {return device;}
bool input_menu_confirm_active(void) {return false;}
int main(void) {
    g_settings.button_icons=ICONS_AUTO;family=3;device=DEVICE_PAD;
    assert(resolve_kind()==ICONS_DECK && deck_diagram_enabled());
    device=DEVICE_KEYBOARD;
    assert(resolve_kind()==ICONS_KEYBOARD && deck_diagram_enabled());
    uint32_t deck_generation=glyphs_generation();
    family=1;assert(!deck_diagram_enabled());assert(glyphs_generation()!=deck_generation);
    family=2;assert(!deck_diagram_enabled());family=0;assert(!deck_diagram_enabled());
    family=3;g_settings.button_icons=ICONS_NINTENDO;assert(!deck_diagram_enabled());
    g_settings.button_icons=ICONS_XBOX;assert(!deck_diagram_enabled());
    g_settings.button_icons=ICONS_DECK;family=0;assert(deck_diagram_enabled());
    return 0;
}
