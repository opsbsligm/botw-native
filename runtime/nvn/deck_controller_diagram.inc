// SPDX-License-Identifier: MIT
// Authored Steam Deck art, paired with botw_deck_layout.py callout transforms.
// No game texture is distributed. Only two exact SDK7 RG8 textures are replaced.
static void deck_paint(uint8_t *lin, uint32_t row, uint32_t w, uint32_t h, Shape shape, int grey) {
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        float a=cover(sd(&shape,x+.5f,y+.5f));
        if (!a) continue;
        uint8_t *p=lin+(uint64_t)y*row+x*2;
        float old=p[1]/255.0f, combined=a+old*(1-a);
        p[0]=(uint8_t)lroundf((grey*a+p[0]*old*(1-a))/combined);
        p[1]=(uint8_t)lroundf(combined*255);
    }
}
// Soft drop shadow: the body's silhouette pushed down and faded with distance.
static void deck_shadow(uint8_t *lin, uint32_t row, uint32_t w, uint32_t h,
                        const Shape *body, float oy, float fade) {
    for (uint32_t y=0;y<h;y++) for (uint32_t x=0;x<w;x++) {
        float d=sd(body,x+.5f,y+.5f-oy);
        float a=d<0?0:cover(d*fade);
        if (!a) continue;
        uint8_t *p=lin+(uint64_t)y*row+x*2;
        float old=p[1]/255.0f, combined=a*0.42f+old*(1-a*0.42f);
        p[0]=(uint8_t)lroundf((4*a*0.42f+p[0]*old*(1-a*0.42f))/combined);
        p[1]=(uint8_t)lroundf(combined*255);
    }
}
static void deck_word(uint8_t *lin,uint32_t row,uint32_t w,uint32_t h,
                      int cx,int cy,const char *label,float cap,float max_w) {
    Canvas c; text(&c,label,CELL/2.0f,CELL/2.0f,cap,max_w);
    for (int y=0;y<CELL;y++) for (int x=0;x<CELL;x++) {
        int X=cx+x-CELL/2,Y=cy+y-CELL/2;
        if(X<0||Y<0||X>=(int)w||Y>=(int)h) continue;
        float a=c.a[y*CELL+x]; uint8_t *p=lin+(uint64_t)Y*row+X*2;
        p[0]=(uint8_t)lroundf(p[0]*(1-a)+246*a);
    }
}
static void deck_draw(uint8_t *lin,uint32_t row,uint32_t w,uint32_t h) {
    for(uint32_t y=0;y<h;y++) memset(lin+(uint64_t)y*row,0,w*2);
    #define DP(shape,grey) deck_paint(lin,row,w,h,(shape),(grey))
    #define DW(x,y,s,cap,mw) deck_word(lin,row,w,h,x,y,s,cap,mw)
    if(h==288) {
        Shape body_shape=box(288,144,275,103,32);
        deck_shadow(lin,row,w,h,&body_shape,3.0f,0.28f);
        DP(body_shape,52);
        DP(box(52,155,40,111,31),60); DP(box(524,155,40,111,31),60);
        DP(box(288,96,275,34,30),60);  // lit top surface
        DP(ring(box(288,144,275,103,32),1.2f),158);
        DP(box(288,143,151,101,9),36); DP(ring(box(288,143,151,101,9),1),150);
        DP(box(288,143,140,88,2),15);
        DW(267,144,"STEAM",10,42); DW(312,144,"DECK",10,36);
        for(int side=0;side<2;side++) {
            int x=side?470:88;
            DP(circle(x,88,17),24); DP(ring(circle(x,88,17),1.5f),185);
            DP(circle(x,88,12),72); DP(ring(circle(x,88,11),.7f),120);
            DW(x,89,side?"R3":"L3",8,19);
            int pad=side?506:70;
            DP(box(pad,173,28,28,5),30); DP(ring(box(pad,173,28,28,5),1),150);
            for(int y=222;y<=236;y+=4) DP(seg(pad-15,y,pad+15,y,.8f),140);
        }
        DP(box(38,65,6,19,2),170); DP(box(38,65,19,6,2),170);
        DP(box(38,65,4,17,1),12); DP(box(38,65,17,4,1),12);
        const int px[4]={517,535,499,517},py[4]={106,89,89,71};
        const char *letters[4]={"A","B","X","Y"};
        for(int i=0;i<4;i++) {
            DP(circle(px[i],py[i],10),16); DP(ring(circle(px[i],py[i],10),.7f),185);
            DW(px[i],py[i],letters[i],10,12);
        }
        DP(box(126,54,10,7,3),22); DP(box(450,54,10,7,3),22);
        DP(ring(box(124,53,3,2.5f,0),.6f),215); DP(ring(box(128,56,3,2.5f,0),.6f),215);
        for(int y=51;y<=57;y+=3) DP(seg(445,y,455,y,.6f),215);
        DP(box(119,231,17,7,5),22); DW(119,231,"STEAM",5,28);
        DP(box(457,231,14,7,5),22); DW(457,230,"...",8,20);
    } else {
        Shape edge_shape=box(288,52,276,25,19);
        deck_shadow(lin,row,w,h,&edge_shape,2.2f,0.30f);
        DP(edge_shape,52);
        DP(box(288,38,276,10,14),60);  // lit top edge
        DP(ring(edge_shape,1),158);
        DP(box(46,25,31,17,10),22); DP(ring(box(46,25,31,17,10),1),158);
        DP(box(529,25,31,17,10),22); DP(ring(box(529,25,31,17,10),1),158);
        DP(box(46,49,34,10,4),72); DP(box(530,49,34,10,4),72);
        DW(46,24,"L2",8,42); DW(529,24,"R2",8,42);
        DW(46,49,"L1",8,42); DW(530,49,"R1",8,42);
        DP(box(135,48,24,5,2),22); DW(124,49,"-",6,10); DW(146,49,"+",6,10);
        for(int x=225;x<310;x+=6) DP(seg(x,44,x,61,.9f),18);
        DP(box(373,50,9,4,2),15); DW(373,73,"USB-C",5,26);
        DP(circle(426,49,5),15); DP(circle(467,49,5),158);
    }
    #undef DP
    #undef DW
}
// The controller help diagram follows hardware, even after keyboard/OSK input.
// Explicit user overrides still win; ordinary glyphs follow the last device.
static bool deck_diagram_enabled(void) {
    return g_settings.button_icons==ICONS_DECK ||
        (g_settings.button_icons==ICONS_AUTO && input_pad_family()==3);
}
// Which of the three ways the Deck artwork fails to appear is otherwise
// invisible here: the feature is off, the font is gone, or the art simply is not
// the texture we keyed on. Log each cause once, and name a near-miss texture by
// the hash actually seen so one remote run is enough to key the next release.
static void deck_skip_reason(const char *art,uint32_t w,uint32_t h,uint64_t seen_hash) {
    static char seen[4][96];
    char line[96];
    if(seen_hash) snprintf(line,sizeof line,"%s %ux%u kept: art differs, hash %016llx",
                           art,w,h,(unsigned long long)seen_hash);
    else snprintf(line,sizeof line,"%s %ux%u kept: %s",art,w,h,
                  !deck_diagram_enabled() ? "icons off/auto-without-Deck-family" : "font not loaded");
    for(int i=0;i<4;i++) if(!strcmp(seen[i],line)) return;   // already reported
    for(int i=0;i<4;i++) if(!seen[i][0]) {                    // first time: note and report
        snprintf(seen[i],sizeof seen[i],"%s",line);
        LOG("[glyphs] %s\n",line);
        return;
    }
    // Past four distinct causes the sheet repeats every upload; stay quiet
    // rather than fill the log, since the causes that matter are already named.
}
static void deck_crop_rg8(const uint8_t *art,uint32_t sh,uint8_t *lin,uint32_t row,
                          uint32_t w,uint32_t h,int sx,int sy,int dy) {
    int left=sx<0?-sx:0,source_left=sx<0?0:sx;
    int count=(int)w-left;
    if(count>576-source_left) count=576-source_left;
    for(uint32_t y=0;y<h;y++) {
        memset(lin+(uint64_t)y*row,0,w*2);
        int source_y=(int)y-dy+sy;
        if(count>0 && source_y>=0 && source_y<(int)sh)
            memcpy(lin+(uint64_t)y*row+(unsigned)left*2,
                   art+((uint64_t)source_y*576+(unsigned)source_left)*2,(unsigned)count*2);
    }
}
// Nine exact SDK7 closeups used by tutorials (five legacy pad views and four
// Switch views). Crops retain their topic: shoulders, face buttons, left side.
// Unknown art is never selected by dimensions alone.
static bool deck_small_diagram(uint32_t format,uint32_t w,uint32_t h,uint8_t *lin,uint32_t row) {
    static const uint64_t identities[9] = {
        0xd2316398fe71cb8aull,0x1ab2de0d50a452ffull,0x243e35faea9d39b6ull,
        0xf4bcab4bac3a2c9cull,0x016c56e2f4a0eb21ull,0x158bd8dd5cc131c3ull,
        0x5ba630897a05fb12ull,0x1c122818005987b1ull,0x1ae381a3464910a6ull
    };
    if(format!=0x0d || w!=h || (w!=188 && w!=228) || row<w*2) return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(uint32_t y=0;y<h;y++) for(uint32_t x=0;x<w*2;x++) hash=(hash^lin[(uint64_t)y*row+x])*0x100000001b3ull;
    int index=-1;
    for(int i=0;i<9;i++) if(identities[i]==hash && w==(i<5?188u:228u)) index=i;
    if(index<0) { deck_skip_reason("SDK7 tutorial closeup",w,h,hash); return false; }
    if(!deck_diagram_enabled() || !font_ready()) { deck_skip_reason("SDK7 tutorial closeup",w,h,0); return true; }
    bool shoulder=index==0||index==1||index==5||index==6;
    uint32_t sh=shoulder?100:288;
    uint8_t *art=malloc(576u*sh*2);
    if(!art) return true;
    deck_draw(art,576*2,576,sh);
    int sx=0,sy=0,dy=shoulder?((int)h-100)/2:0;
    if(index==1||index==6) sx=576-(int)w;
    if(index==2) {sx=388;sy=30;}
    if(index==7) {sx=348;sy=16;}
    // Exact original Minus rectangle bounds132..150 /54..61 give centre
    // (141,57.5). View at(126,54) becomes(141,58), within half a source pixel
    // of the existing animated highlight; pad/crop instead of stretching art.
    if(index==8) {sx=-15;sy=-4;}
    deck_crop_rg8(art,sh,lin,row,w,h,sx,sy,dy);
    free(art);
    LOG("[glyphs] SDK7 Deck tutorial closeup %d (%ux%u): exact content match\n",index,w,h);
    return true;
}
static bool deck_controller_diagram(uint32_t format,uint32_t w,uint32_t h,uint8_t *lin,uint32_t row) {
    if(deck_small_diagram(format,w,h,lin,row)) return true;
    if(format!=0x0d||w!=576||(h!=288&&h!=100)||row<w*2) return false;
    uint64_t hash=0xcbf29ce484222325ull;
    for(uint32_t y=0;y<h;y++) for(uint32_t x=0;x<w*2;x++) hash=(hash^lin[(uint64_t)y*row+x])*0x100000001b3ull;
    if(hash!=(h==288?0x8b42a6ff21f5dea4ull:0x6e2b71b7ed623592ull)) {
        deck_skip_reason("SDK7 pad diagram",w,h,hash);
        return false;
    }
    if(!deck_diagram_enabled()||!font_ready()) { deck_skip_reason("SDK7 pad diagram",w,h,0); return true; }
    deck_draw(lin,row,w,h);
    LOG("[glyphs] SDK7 Steam Deck diagram %ux%u: matched ABXY, sticks, touchpads, physical ABXY, L1/R1/L2/R2\n",w,h);
    return true;
}
