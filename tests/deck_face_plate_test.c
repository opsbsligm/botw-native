/* SPDX-License-Identifier: MIT */
/* Deck face prompts are the rounded square of the physical button, not the game's disc.
 * Calls the production draw_glyph; the font stub keeps labels out so the plate geometry is what scores. */
#include "../runtime/nvn/glyphs.c"
#include <assert.h>

Settings g_settings = {0};
int16_t g_bind_key[BIND_KEY_COUNT] = {0}, g_bind_pad[BIND_PAD_COUNT] = {0};
int input_pad_family(void) { return 3; }
int input_last_device(void) { return DEVICE_PAD; }
bool input_menu_confirm_active(void) { return false; }
bool rt_resource(const char *n, const char *d, char *o, size_t k) {
    (void)n; (void)d; (void)o; (void)k;
    return false;  /* no font asset: the plate is measured uncovered */
}
const char *bind_pad_label(int host, int fam) { (void)host; (void)fam; return "?"; }

static float paint(const Canvas *c, int x, int y) { return c->a[y * CELL + x]; }

/* Lit bounding box of a plate, to catch a redraw that resizes the cell's art. */
static void bbox(const Canvas *c, int *x0, int *y0, int *x1, int *y1) {
    *x0 = *y0 = CELL; *x1 = *y1 = -1;
    for (int y = 0; y < CELL; y++) for (int x = 0; x < CELL; x++) if (c->a[y * CELL + x] > .5f) {
        if (x < *x0) *x0 = x; if (x > *x1) *x1 = x;
        if (y < *y0) *y0 = y; if (y > *y1) *y1 = y;
    }
}

int main(void) {
    g_bind_pad[BIND_A] = SDL_GAMEPAD_BUTTON_SOUTH;  /* the default binding for A */
    const Box b = {25, 25, 40, 40};                 /* a face glyph's box, as measured from the sheet */
    Canvas dk, xb;
    int copy = 0;

    memset(&dk, 0, sizeof dk);   /* the caller clears; fill() only raises coverage */
    assert(draw_glyph(&dk, b, 0xE040, ICONS_DECK, &copy));
    assert(!copy);
    memset(&xb, 0, sizeof xb);
    assert(draw_glyph(&xb, b, 0xE040, ICONS_XBOX, &copy));

    /* Same footprint: the square fills the disc's box, so a redraw cannot resize a prompt. */
    int a0, a1, a2, a3, c0, c1, c2, c3;
    bbox(&dk, &a0, &a1, &a2, &a3);
    bbox(&xb, &c0, &c1, &c2, &c3);
    assert(a2 - a0 == c2 - c0 && a3 - a1 == c3 - c1);
    assert(!paint(&dk, 0, 0) && !paint(&dk, CELL - 1, CELL - 1));  // the cell gutter stays clean

    /* The diagonal 15 pixels out from the centre separates the two shapes: still inside the square
     * (the corner arc is that far out), already outside a disc of the same box. Deck has to be solid
     * there and Xbox empty, so the test fails if either plate shape drifts back to the other. */
    assert(paint(&dk, 40, 40) > .5f);           /* 25+15 in both axes */
    assert(paint(&xb, 40, 40) < .25f);
    assert(paint(&dk, 10, 40) > .5f);           /* mirrored corners too */
    assert(paint(&dk, 40, 10) > .5f);

    /* The plate is solid to within a pixel of the box edge on the axes, like the disc it replaces. */
    assert(paint(&dk, 25, 7) > .5f && paint(&dk, 25, 42) > .5f);
    assert(paint(&dk, 7, 25) > .5f && paint(&dk, 42, 25) > .5f);

    /* Stick presses follow the faces (they are buttons the player presses, not the stick itself). */
    memset(&dk, 0, sizeof dk);
    g_bind_pad[BIND_LSTICK] = SDL_GAMEPAD_BUTTON_LEFT_STICK;
    assert(draw_glyph(&dk, b, 0xE056, ICONS_DECK, &copy));
    assert(paint(&dk, 40, 40) > .5f);

    return 0;
}
