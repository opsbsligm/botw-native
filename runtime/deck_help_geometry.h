/* SPDX-License-Identifier: MIT */
/* Geometry for the authored Deck diagram. Only called on a pinned private layout.
 * Labels keep their original positions; leaders end at the relocated controls.
 * Walking, horse and sand-seal groups retain their original animation channels. */
#pragma once
#include <stdlib.h>

typedef struct { size_t at; int parent,depth; } NdHelpPane;
static float nd_help_float(const uint8_t *p) { float value;memcpy(&value,p,4);return value; }
static void nd_help_set_float(uint8_t *p,double value) { float f=(float)value;memcpy(p,&f,4); }
static int nd_help_panes(const uint8_t *b,size_t n,NdHelpPane out[512]) {
    if(n<20 || memcmp(b,"FLYT\xff\xfe",6) || nd_help_u32(b+12)!=n) return -1;
    size_t at=nd_help_u16(b+6);int count=0,stack[32],depth=0,last=-1;
    if(at<20 || at>n) return -1;
    while(at<n) {
        if(n-at<8) return -1;
        size_t size=nd_help_u32(b+at+4);
        if(size<8 || size>n-at) return -1;
        if(!memcmp(b+at,"pan1",4) || !memcmp(b+at,"pic1",4) || !memcmp(b+at,"txt1",4) ||
           !memcmp(b+at,"bnd1",4) || !memcmp(b+at,"wnd1",4) || !memcmp(b+at,"prt1",4)) {
            if(size<84 || count==512 || !memchr(b+at+12,0,24)) return -1;
            for(unsigned field=0;field<10;field++) if(!isfinite(nd_help_float(b+at+44+4*field))) return -1;
            out[count]=(NdHelpPane){at,depth?stack[depth-1]:-1,depth};last=count++;
        } else if(!memcmp(b+at,"pas1",4)) {
            if(last<0 || depth==32) return -1;
            stack[depth++]=last;
        } else if(!memcmp(b+at,"pae1",4)) {
            if(!depth) return -1;
            depth--;
        }
        at+=size;
    }
    return depth?-1:count;
}
static int nd_help_legacy_geometry(uint8_t *b,const NdHelpPane *p,int count) {
    int groups=0,connectors=0;
    for(int i=0;i<count;i++) {
        size_t at=p[i].at;const char *name=(const char *)b+at+12;
        const char *parent=p[i].parent<0?"":(const char *)b+p[p[i].parent].at+12;
        double dx=0,dy=0;
        if(!strncmp(name,"N_OpenRStickPress_",18)) {dx=-31.49;dy=42.88;}
        else if(!strncmp(name,"N_CrossKey_",11)) {dx=-13.4;dy=58.29;}
        else if(p[i].depth==3 && (!strcmp(name,"N_Line_00") || !strcmp(name,"N_Line_08") || !strcmp(name,"N_Line_10"))) dx=20.1;
        else if(!strncmp(name,"N_OpenPlusMinus_",16)) dx=-29.48;
        else if(!strncmp(name,"N_Minus_",8)) dx=59.63;
        if(dx || dy) {
            nd_help_set_float(b+at+44,nd_help_float(b+at+44)+dx);
            nd_help_set_float(b+at+48,nd_help_float(b+at+48)+dy);groups++;
        }
        if(!memcmp(b+at,"pic1",4) && (!strcmp(parent,"N_Line_00") || !strcmp(parent,"N_Line_08") || !strcmp(parent,"N_Line_10"))) {
            float x=nd_help_float(b+at+44);
            if(x<=-50) {nd_help_set_float(b+at+44,x-20.1);connectors++;}
            else if(x==-16 && nd_help_float(b+at+64)==0) {nd_help_set_float(b+at+76,nd_help_float(b+at+76)+20.1);connectors++;}
        }
    }
    return groups==15 && connectors==12;
}
/* Only geometry fields are edited. Animated brackets and translated text remain
 * owned by the game, including the different one/two/three-row D-pad variants. */
static int nd_help_geometry_impl(uint8_t *b,size_t n) {
    NdHelpPane panes[512];int count=nd_help_panes(b,n,panes);
    if(count<0) return 0;
    static const char *const names[]={
        "N_CrossKey_00","N_CrossKey_01","N_CrossKey_02",
        "Nt_TipsLineCircle_03","Nt_TipsLineCircle_05","Nt_TipsLineCircle_01",
        "P_Line_15","P_Line_134","P_Line_14",
        "P_Line_139","P_Line_140","P_Line_138","P_Line_141",
        "N_OpenRStickPress_00","N_OpenRStickPress_01","N_OpenRStickPress_02",
        "P_Line_07","P_Line_20","P_Line_163",
        "P_Line_08","P_Line_21","P_Line_164",
        "P_Line_09","P_Line_27","P_Line_165",
        "P_Line_13","P_Line_98","P_Line_169"
    };
    size_t found[28]={0};uint32_t seen=0;
    for(int i=0;i<count;i++) for(unsigned j=0;j<28;j++) if(!strcmp((const char *)b+panes[i].at+12,names[j])) {
        if(seen&(1u<<j)) return 0;
        found[j]=panes[i].at;seen|=1u<<j;
    }
    if(seen!=0x0fffffffu) return 0;
    for(unsigned j=0;j<28;j++) {
        uint8_t *p=b+found[j];
        if(j<3) {nd_help_set_float(p+44,-122);nd_help_set_float(p+48,-15);}
        else if(j<6) {nd_help_set_float(p+44,-45.4);nd_help_set_float(p+48,58.29);}
        else if(j<9) {
            double sy=j==6?0:j==7?-39:-44,dx=22.6,dy=58.29-sy;
            double distance=hypot(dx,dy),length=distance-32;
            nd_help_set_float(p+44,-68+dx*length/distance);
            nd_help_set_float(p+48,sy+dy*length/distance);
            nd_help_set_float(p+64,atan2(dy,dx)*180/3.14159265358979323846);
            nd_help_set_float(p+76,length);
        } else if(j<13) nd_help_set_float(p+76,0);
        else if(j<16) {nd_help_set_float(p+44,136);nd_help_set_float(p+48,-125);}
        else if(j<19) {nd_help_set_float(p+44,13.99);nd_help_set_float(p+48,152.88);}
        else if(j<22) {nd_help_set_float(p+44,13.99);nd_help_set_float(p+48,147.88);nd_help_set_float(p+76,113.88);}
        else if(j<25) nd_help_set_float(p+44,13.99);
        else {nd_help_set_float(p+44,8.99);nd_help_set_float(p+76,62.99);}
    }
    return 28;
}
