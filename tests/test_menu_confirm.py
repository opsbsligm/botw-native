"""Exercise the production input policy against synthetic guest memory."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class MenuConfirm(unittest.TestCase):
 def test_menu_transition_gameplay_and_invalid_memory(self):
  with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
   p=Path(temp);c=p/'main.c';exe=p/'check'
   c.write_text(r'''
#include "menu_confirm.h"
#include <assert.h>
#include <string.h>
static unsigned char mem[0x3000];
static uint64_t mgr=0x7240000000ull,arr=0x7240000100ull,obj=0x7240001000ull;
static int bad;
static bool rd(void *u,uint64_t p,void *out,size_t n) {
 (void)u;if(bad)return false;
 if(p==0x7002cc2490ull&&n==8){memcpy(out,&mgr,8);return true;}
 const unsigned ids[]={1,0x2e,0x39,0x4a};
 const char *names[]={"Title_00","PauseMenu_00","SystemWindow_00","MessageDialog"};
 for(unsigned i=0;i<4;i++){
  uint64_t label=0x7002000001ull+0x100*i; // strings need not be aligned
  if(p==0x7002bfeb78ull+ids[i]*8&&n==8){memcpy(out,&label,8);return true;}
  if(p==label&&n==strlen(names[i])+1){memcpy(out,names[i],n);return true;}
 }
 if(p>=mgr && p< mgr+sizeof mem && n<=mgr+sizeof mem-p){memcpy(out,mem+(p-mgr),n);return true;}
 return false;
}
static void put(unsigned off,const void *v,size_t n){memcpy(mem+off,v,n);}
int main(void) {
 NdMenuConfirm s={0};
 assert(nd_menu_confirm(&s,false,1,1,2)==1); // original physical east gameplay A
 assert(nd_menu_confirm(&s,true,1,1,2)==0); // held interact cannot cancel new dialog
 assert(nd_menu_confirm(&s,true,0,1,2)==0);
 assert(nd_menu_confirm(&s,true,2,1,2)==1); // physical south(A) confirms
 assert(nd_menu_confirm(&s,true,1,1,2)==2); // physical east(B) cancels
 assert(nd_menu_confirm(&s,false,1,1,2)==0); // exit cannot immediately interact
 assert(nd_menu_confirm(&s,false,0,1,2)==0);
 assert(nd_menu_confirm(&s,false,2,1,2)==2); // original south gameplay sprint
 assert(nd_menu_confirm(&s,false,0,8,16)==0);
 assert(nd_menu_confirm(&s,false,1,8,16)==8); // custom gameplay bindings retained
 assert(nd_menu_confirm(&s,true,1,8,16)==0);
 assert(nd_menu_confirm(&s,true,0,8,16)==0);
 assert(nd_menu_confirm(&s,true,3,8,16)==3);
 unsigned count=99;put(0x28,&count,4);put(0x30,&arr,8);
 put(0x100+8*0x2e,&obj,8);mem[0x10fe]=2;
 assert(nd_botw_menu_context(rd,0));
 mem[0x10fe]=1;assert(!nd_botw_menu_context(rd,0)); // opening is not yet interactive
 mem[0x10fe]=2;count=100;put(0x28,&count,4);assert(!nd_botw_menu_context(rd,0));
 count=99;put(0x28,&count,4);bad=1;assert(!nd_botw_menu_context(rd,0));bad=0;
 uint64_t invalid=~0ull;put(0x100+8*0x2e,&invalid,8);assert(!nd_botw_menu_context(rd,0));
 put(0x100+8*0x2e,&(uint64_t){0},8);
 put(0x100+8*0x25,&obj,8);assert(!nd_botw_menu_context(rd,0)); // world HUD never swaps
 put(0x100+8*0x2d,&obj,8);assert(!nd_botw_menu_context(rd,0)); // quick weapon controls unchanged
 assert(!nd_botw_menu_context(0,0));
 return 0;
}
''')
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'runtime'),str(c),str(ROOT/'runtime/menu_confirm.c'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
