/* SPDX-License-Identifier: MIT */
/* Deck face prompts are the rounded square of the physical button, not the game's disc.
 * Calls the production draw_glyph. With no argument the font stub keeps labels out, so the plate
 * geometry alone is what scores; with "letter" the real Fira Sans is loaded (through the path the
 * binary was given in NATIVE_FONT) and the knocked-out capital is measured as drawn. */
#include "../runtime/nvn/glyphs.c"
#include <assert.h>

Settings g_settings = {0};
int16_t g_bind_key[BIND_KEY_COUNT] = {0}, g_bind_pad[BIND_PAD_COUNT] = {0};
int input_pad_family(void) { return 3; }
int input_last_device(void) { return DEVICE_PAD; }
bool input_menu_confirm_active(void) { return false; }
/* The letter test measures a real glyph, so it needs the font the runtime would load. CI has no
 * installed copy, so it comes from the source tree; NATIVE_FONT overrides it for a local run. */
static const char *g_font_path;
static bool font_available(void) {
    static const char *rel[] = {"third_party/fonts/FiraSans/FiraSans-SemiBold.ttf",
                                "../third_party/fonts/FiraSans/FiraSans-SemiBold.ttf",
                                "../../third_party/fonts/FiraSans/FiraSans-SemiBold.ttf"};
    const char *env = getenv("NATIVE_FONT");
    if (env && *env) { g_font_path = env; return true; }
    char probe[1024];
    for (size_t i = 0; i < sizeof rel / sizeof *rel; i++) {
        snprintf(probe, sizeof probe, "%s", rel[i]);
        FILE *f = fopen(probe, "rb");
        if (f) { fclose(f); g_font_path = rel[i]; return true; }
    }
    return false;
}
bool rt_resource(const char *n, const char *d, char *o, size_t k) {
    if (!g_font_path) return false;  /* no font asset: the plate is measured uncovered */
    snprintf(o, k, "%s", g_font_path);
    return true;
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

/* Where the plate's outer edge crosses a ray from the cell centre, in pixels. The ray runs inward
 * and stops at the first covered pixel, so the letter knocked out of the middle -- and the gap
 * between the two characters of a pair like "L3" -- cannot move the reading. Sub-pixel by
 * interpolating the coverage ramp, so a corner-radius change shows as a fraction of a pixel. */
static float edge(const Canvas *c, float dx, float dy) {
    float prev = 0.f;
    for (float t = 34.f; t > 0.f; t -= 0.05f) {
        int x = (int)(25 + dx * t), y = (int)(25 + dy * t);
        if (x < 0 || x >= CELL || y < 0 || y >= CELL) { prev = 0.f; continue; }
        float v = c->a[y * CELL + x];
        if (v >= .5f) { float f = (.5f - prev) / (v - prev); return t + .05f - f * .05f; }
        prev = v;
    }
    return -1.f;
}

int main(int argc, char **argv) {
    bool letter_test = argc > 1 && !strcmp(argv[1], "letter");
    if (letter_test) assert(font_available() && "letter test needs Fira Sans (run from the repo root or set NATIVE_FONT)");

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

    if (!letter_test) return 0;

    /* --- the plate is a squircle, not a disc: where its edge crosses the diagonal separates them.
     * With half width h and corner r the distance is (h-r)*sqrt2 + r; at h=20 that is 23.48 for the
     * accepted r=9h/16 and 22.92 for the old r=0.32*plate=12.8. Both sit inside a disc's 20 by
     * construction, so only the diagonal can tell them apart -- and a redraw that rounds the corner
     * back toward a disc has to fail here rather than ship prompts that read as Switch's own. */
    float diag = edge(&dk, 0.70710678f, 0.70710678f), axis = edge(&dk, 0.f, 1.f);
    assert(axis > 19.f && axis < 21.f);              /* the footprint on the axes is the box edge */
    printf("diag %.3f axis %.3f ratio %.3f\n", diag, axis, diag / axis);
    assert(diag / axis > 1.175f && diag / axis < 1.235f);

    /* --- the knocked-out capital: measured as drawn, in fractions of the plate. The accepted masks
     * in the sibling project sit at 0.645 of the plate; a letter at half the plate (what this file
     * drew before) reads small and weak at the ~40 pixels a prompt occupies on 1280x800, and one at
     * three quarters crowds the rim. Both limits are checked with the font the runtime would load. */
    /* Draw the same glyph twice, with and without the font, and score the pixels the letter removed:
     * the empty corners of the rounded square are uncovered but are not the letter, so an absolute
     * scan would over-read the cap. font_ready() caches its result, so each pass clears that cache
     * along with the font path, or the plate-only pass inherits the first pass's label and the
     * difference comes out empty. */
    memset(&dk, 0, sizeof dk);
    g_font_state = 0;
    assert(draw_glyph(&dk, b, 0xE040, ICONS_DECK, &copy));   /* plate + letter */
    int h0, h1, h2, h3;
    bbox(&dk, &h0, &h1, &h2, &h3);
    Canvas letters = dk;
    memset(&dk, 0, sizeof dk);
    const char *font = g_font_path; g_font_path = NULL; g_font_state = 0;
    assert(draw_glyph(&dk, b, 0xE040, ICONS_DECK, &copy));   /* plate alone */
    g_font_path = font; g_font_state = 0;
    int l0 = CELL, r0 = -1, t0 = CELL, b0 = -1;
    for (int y = 0; y < CELL; y++) for (int x = 0; x < CELL; x++)
        if (paint(&letters, x, y) < .5f && paint(&dk, x, y) >= .5f) {  /* the letter is the hole */
            if (x < l0) l0 = x; if (x > r0) r0 = x; if (y < t0) t0 = y; if (y > b0) b0 = y;
        }
    assert(r0 > l0 && b0 > t0);                      /* a letter is actually knocked out */
    int plate = h2 - h0 + 1, cap = b0 - t0 + 1, wid = r0 - l0 + 1;
    float ratio = (float)cap / (float)plate, wratio = (float)wid / (float)plate;
    float clear = (float)((t0 - h1) < (h3 - b0) ? (t0 - h1) : (h3 - b0)) / (float)plate;
    printf("plate %d cap %d (%.3f) width %.3f clearance %.3f\n", plate, cap, ratio, wratio, clear);
    assert(ratio > 0.58f && ratio < 0.72f);
    assert(clear > 0.10f);                           /* the letter stays clear of the rim */
    assert(wratio < 0.80f);                          /* and inside the plate on the widest capital */
    return 0;
}
