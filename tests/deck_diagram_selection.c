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
bool rt_resource(const char *name,const char *dev_rel,char *out,size_t n) {
    (void)name;(void)dev_rel;(void)out;(void)n;
    return false; // Selection/unknown-content tests do not load a font asset.
}
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
    uint8_t unknown[228*228*2];memset(unknown,17,sizeof unknown);
    assert(!deck_small_diagram(0x0d,228,228,unknown,228*2));
    for(unsigned i=0;i<sizeof unknown;i++) assert(unknown[i]==17);
    assert(!deck_small_diagram(0x0d,228,228,unknown,228*2-1));
    assert(!deck_small_diagram(0x01,228,228,unknown,228*2));
    // Negative crop offsets pad the artwork, never read before its buffer or
    // overwrite row padding. This is the measured View/Minus tutorial fix.
    uint8_t art[576*100*2],cropped[(228*2+8)*228+32];
    for(unsigned i=0;i<sizeof art;i++) art[i]=(uint8_t)(i*13u);
    memset(cropped,0xa5,sizeof cropped);
    deck_crop_rg8(art,100,cropped+16,228*2+8,228,228,-15,-4,0);
    for(unsigned y=0;y<228;y++) {
        for(unsigned x=0;x<228;x++) for(unsigned c=0;c<2;c++) {
            uint8_t expected=(y>=4 && y<104 && x>=15)?art[((y-4)*576+x-15)*2+c]:0;
            assert(cropped[16+y*(228*2+8)+x*2+c]==expected);
        }
        for(unsigned x=228*2;x<228*2+8;x++) assert(cropped[16+y*(228*2+8)+x]==0xa5);
    }
    for(unsigned i=0;i<16;i++) assert(cropped[i]==0xa5 && cropped[sizeof cropped-16+i]==0xa5);
    return 0;
}
