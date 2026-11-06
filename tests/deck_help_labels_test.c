/* Own synthetic FLYT sections; no game layout bytes. MIT. */
#include <assert.h>
#include <stdio.h>
#include "deck_help_labels.h"
static void u32(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
int main(void) {
    static const char *names[] = {"T_Icon_00","T_Icon_02","T_Icon_03","T_Icon_01","T_Icon_12","T_Icon_13",
        "T_Icon_28","T_Icon_29","T_Icon_25","T_Icon_26","T_IconXB_09","T_IconXB_10","Footer"};
    const uint16_t codes[] = {0xe042,0xe040,0xe041,0xe043,0xe042,0xe040,0xe041,0xe043,0xe040,0xe041,0xe042,0xe040,0xe040};
    enum { N=20+13*180 }; uint8_t original[N], changed[N], broken[N]; memset(original,0,N);
    memcpy(original,"FLYT\xff\xfe",6);original[6]=20;u32(original+12,N);
    for(unsigned i=0;i<13;i++) {
        uint8_t *p=original+20+i*180;memcpy(p,"txt1",4);u32(p+4,180);strcpy((char *)p+12,names[i]);
        p[0x5a]=1;u32(p+0x64,168);p[168]=(uint8_t)codes[i];p[169]=(uint8_t)(codes[i]>>8);
    }
    memcpy(changed,original,N);assert(nd_help_text(changed,N,"BAYX")==0);assert(!memcmp(changed,original,N));
    assert(nd_help_text_impl(changed,N,"BAYX")==12);
    for(unsigned i=0;i<12;i++) {
        uint8_t *p=changed+20+i*180;assert(p[0x5a]==0 && p[0x5b]==0);
        assert(p[168]=="BAYX"[codes[i]-0xe040] && p[169]==0);
        // Every byte outside font index and one UTF16 code unit is retained.
        for(unsigned j=0;j<180;j++) if(j!=0x5a && j!=0x5b && j!=168 && j!=169)
            assert(p[j]==original[20+i*180+j]);
    }
    assert(!memcmp(changed+20+12*180,original+20+12*180,180));
    memcpy(broken,original,N);broken[20+11*180+168]=0xff;memcpy(changed,broken,N);
    assert(nd_help_text_impl(changed,N,"BAYX")==0 && !memcmp(changed,broken,N));
    memcpy(changed,original,N);assert(nd_help_text_impl(changed,N-1,"BAYX")==0);
    assert(nd_help_text_impl(changed,N,"BA?X")==0 && !memcmp(changed,original,N));
    puts("World help labels match physical bindings; menu footer untouched; malformed layouts leave no partial edits");
    return 0;
}
