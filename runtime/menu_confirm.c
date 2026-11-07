/* Original native menu policy and bounded screen-state reader, MIT. */
#include "menu_confirm.h"
#include <string.h>
uint64_t nd_menu_confirm(NdMenuConfirm *s, bool active, unsigned held,
                         uint64_t east_actions, uint64_t south_actions) {
    held &= 3;
    if (s->active != active) s->blocked |= held;
    s->active = active;
    s->blocked &= held;
    unsigned available = held & ~s->blocked;
    return ((available & 1) ? (active ? 2 : east_actions) : 0) |
           ((available & 2) ? (active ? 1 : south_actions) : 0);
}
static bool get(NdMenuRead read, void *u, uint64_t p, void *out, size_t n) {
    const uint64_t lo=0x7000000000ull,hi=0x7320000000ull;
    return p>=lo && p<hi && n<=hi-p && read(u,p,out,n);
}
static bool pointer(uint64_t p) { return p>=0x7000000000ull && p<0x7320000000ull-0x100 && !(p&7); }
bool nd_botw_menu_context(NdMenuRead read, void *u) {
    if (!read) return false;
    /* Local 1.6.0 name lookup + ScreenMgr dispatch establish this table/index ABI.
       Runtime also checks selected catalog labels, count, all read ranges and visibility. */
    static const struct { unsigned id; const char *name; } pins[] = {
        {1,"Title_00"},{0x2e,"PauseMenu_00"},{0x39,"SystemWindow_00"},{0x4a,"MessageDialog"}
    };
    uint64_t p;
    for (size_t i=0;i<sizeof pins/sizeof *pins;i++) {
        char text[32]={0}; size_t n=strlen(pins[i].name)+1;
        if (!get(read,u,0x7002bfeb78ull+8*pins[i].id,&p,8) ||
            !get(read,u,p,text,n) || memcmp(text,pins[i].name,n)) return false;
    }
    uint64_t mgr,arr,again,screens[99]; uint32_t count;
    if (!get(read,u,0x7002cc2490ull,&mgr,8) || !pointer(mgr) ||
        !get(read,u,mgr+0x28,&count,4) || count!=99 ||
        !get(read,u,mgr+0x30,&arr,8) || !pointer(arr) ||
        !get(read,u,arr,screens,sizeof screens)) return false;
    /* Explicit interactive screens only; HUD, pickup tips, pause backgrounds and
       MainShortCut are deliberately absent so gameplay/quick weapon controls stay original. */
    /* AppMap_00 (0x2a) also stays open for the WORLD minimap. It is not an
       independent modal input surface; the full Slate map opens AppHome_00
       (0x2c), which remains in this list. */
    static const unsigned ids[]={1,0x13,0x14,0x15,0x17,0x20,0x21,0x22,0x2c,
        0x2e,0x2f,0x30,0x32,0x34,0x35,0x37,0x38,0x39,0x3b,0x3c,0x3d,0x3e,0x3f,0x4a,0x4f};
    bool active=false;
    for (size_t i=0;i<sizeof ids/sizeof *ids;i++) {
        unsigned char state=0; p=screens[ids[i]];
        if (!p) continue;
        if (!pointer(p) || !get(read,u,p+0xfe,&state,1)) return false;
        if (state==2) active=true;
    }
    return get(read,u,0x7002cc2490ull,&again,8) && again==mgr && active;
}
