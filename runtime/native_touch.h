/* SPDX-License-Identifier: MIT */
/* One physical touch owns a complete UI gesture. No SDL/ImGui state crosses threads. */
#pragma once
#include <stdint.h>
#include <math.h>
typedef struct {uint64_t device,finger;int active;} NdTouch;
typedef struct {int consumed,emit,down,cancel;float x,y;} NdTouchResult;
enum {ND_TOUCH_DOWN,ND_TOUCH_MOVE,ND_TOUCH_UP,ND_TOUCH_CANCEL};
static inline void nd_touch_reset(NdTouch *s) {s->active=0;}
static inline NdTouchResult nd_touch_route(NdTouch *s,int type,uint64_t device,uint64_t finger,
                                          float x,float y,float width,float height,int hit,int open) {
    NdTouchResult r={0,0,0,0,0,0};
    int valid=isfinite(x) && isfinite(y) && width>0 && height>0;
    if(type==ND_TOUCH_DOWN) {
        if(s->active) {r.consumed=1;return r;}
        if(!valid || x<0 || y<0 || x>1 || y>1 || (!hit && !open)) return r;
        s->active=1;s->device=device;s->finger=finger;
    } else if(!s->active || s->device!=device || s->finger!=finger) {
        r.consumed=s->active || open;return r;
    }
    r.consumed=r.emit=1;r.down=type==ND_TOUCH_DOWN;
    r.cancel=type==ND_TOUCH_CANCEL || !valid;
    r.x=valid?fminf(fmaxf(x,0),1)*width:0;
    r.y=valid?fminf(fmaxf(y,0),1)*height:0;
    if(type==ND_TOUCH_UP || r.cancel) s->active=0;
    return r;
}

/* Gamescope/X11 may expose a touchscreen gesture as ordinary mouse events.
 * Retain its initial owner, including when a collapsed badge is dragged out
 * of its old bounds. A press begun in the world must stay with the world. */
typedef struct {NdTouch ui;uint64_t outside_device;int outside;} NdMouse;
static inline void nd_mouse_reset(NdMouse *s) {nd_touch_reset(&s->ui);s->outside=0;}
static inline NdTouchResult nd_mouse_route(NdMouse *s,int type,uint64_t device,
                                          float x,float y,float width,float height,int hit,int open) {
    NdTouchResult r={0,0,0,0,0,0};
    int valid=isfinite(x) && isfinite(y) && isfinite(width) && isfinite(height) && width>0 && height>0;
    if(type==ND_TOUCH_CANCEL) {
        if(s->ui.active) r=nd_touch_route(&s->ui,type,s->ui.device,0,0,0,1,1,0,open);
        s->outside=0;return r;
    }
    if(s->outside) {
        if(type==ND_TOUCH_UP && device==s->outside_device) s->outside=0;
        r.consumed=open;return r;
    }
    if(s->ui.active || type==ND_TOUCH_DOWN) {
        r=nd_touch_route(&s->ui,type,device,0,valid?x/width:NAN,valid?y/height:NAN,
                         valid?width:0,valid?height:0,hit,open);
        if(type==ND_TOUCH_DOWN && !r.consumed) {s->outside=1;s->outside_device=device;}
        return r;
    }
    /* Hover and an unpaired release may update an open UI, but never start
       a gesture or steal a world release just because it ends on the badge. */
    if(valid && (open || (hit && type==ND_TOUCH_MOVE))) {
        r.consumed=r.emit=1;r.x=fminf(fmaxf(x,0),width);r.y=fminf(fmaxf(y,0),height);
    }
    return r;
}
