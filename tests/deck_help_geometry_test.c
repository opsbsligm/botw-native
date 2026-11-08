/* SPDX-License-Identifier: MIT */
/* Own flat pane fixture. No game archive/layout bytes. */
#include <assert.h>
#include <stdio.h>
#include "deck_help_labels.h"
static const char *const names[]={"N_CrossKey_00","N_CrossKey_01","N_CrossKey_02",
 "Nt_TipsLineCircle_03","Nt_TipsLineCircle_05","Nt_TipsLineCircle_01",
 "P_Line_15","P_Line_134","P_Line_14","P_Line_139","P_Line_140","P_Line_138","P_Line_141",
 "N_OpenRStickPress_00","N_OpenRStickPress_01","N_OpenRStickPress_02",
 "P_Line_07","P_Line_20","P_Line_163","P_Line_08","P_Line_21","P_Line_164",
 "P_Line_09","P_Line_27","P_Line_165","P_Line_13","P_Line_98","P_Line_169","UntouchedLabel"};
enum {SIZE=20+29*84};
static void u32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));}
static uint8_t *pane(uint8_t *b,int i) {return b+20+84*i;}
static void fixture(uint8_t *b) {
 memset(b,0,SIZE);memcpy(b,"FLYT\xff\xfe",6);b[6]=20;u32(b+12,SIZE);
 for(int i=0;i<29;i++) {
  uint8_t *p=pane(b,i);memcpy(p,"pic1",4);u32(p+4,84);strcpy((char *)p+12,names[i]);
  nd_help_set_float(p+44,3);nd_help_set_float(p+48,4);nd_help_set_float(p+68,1);nd_help_set_float(p+72,1);
  nd_help_set_float(p+76,10);nd_help_set_float(p+80,10);
 }
}
static void close_to(double a,double b) {assert(fabs(a-b)<.002);}
int main(void) {
 uint8_t original[SIZE],b[SIZE],copy[SIZE];fixture(original);memcpy(b,original,SIZE);
 assert(nd_help_geometry_impl(b,SIZE)==28);
 for(int i=0;i<3;i++) {
  uint8_t *root=pane(b,i),*circle=pane(b,i+3),*line=pane(b,i+6);
  close_to(nd_help_float(root+44),-122);close_to(nd_help_float(root+48),-15);
  close_to(nd_help_float(root+44)+nd_help_float(circle+44),-167.4);
  close_to(nd_help_float(root+48)+nd_help_float(circle+48),43.29);
  double angle=nd_help_float(line+64)*3.14159265358979323846/180,length=nd_help_float(line+76);
  double ex=nd_help_float(line+44),ey=nd_help_float(line+48),sy=i==0?0:i==1?-39:-44;
  close_to(ex-cos(angle)*length,-68);close_to(ey-sin(angle)*length,sy);
  close_to(hypot(ex-nd_help_float(circle+44),ey-nd_help_float(circle+48)),32);
  close_to(nd_help_float(pane(b,13+i)+44)-nd_help_float(pane(b,16+i)+44),122.01);
  close_to(nd_help_float(pane(b,13+i)+48)+nd_help_float(pane(b,16+i)+48),27.88);
  close_to(nd_help_float(pane(b,19+i)+48)-nd_help_float(pane(b,19+i)+76),34);
  close_to(nd_help_float(pane(b,25+i)+44)-nd_help_float(pane(b,25+i)+76),-54);
 }
 assert(!memcmp(pane(b,28),pane(original,28),84));
 for(int i=0;i<28;i++) for(unsigned j=0;j<84;j++)
  if(j<44 || (j>=52 && j<64) || (j>=68 && j<76) || j>=80) assert(pane(b,i)[j]==pane(original,i)[j]);
 memcpy(copy,b,SIZE);assert(nd_help_geometry_impl(b,SIZE)==28 && !memcmp(copy,b,SIZE));
 memcpy(b,original,SIZE);strcpy((char *)pane(b,27)+12,"Missing");memcpy(copy,b,SIZE);
 assert(!nd_help_geometry_impl(b,SIZE) && !memcmp(copy,b,SIZE));
 memcpy(b,original,SIZE);strcpy((char *)pane(b,27)+12,names[0]);memcpy(copy,b,SIZE);
 assert(!nd_help_geometry_impl(b,SIZE) && !memcmp(copy,b,SIZE));
 memcpy(b,original,SIZE);nd_help_set_float(pane(b,28)+44,NAN);memcpy(copy,b,SIZE);
 assert(!nd_help_geometry_impl(b,SIZE) && !memcmp(copy,b,SIZE));
 assert(!nd_help_geometry_impl(original,SIZE-1));
 memcpy(b,original,SIZE);assert(!nd_help_text(b,SIZE,"BAYX") && !memcmp(b,original,SIZE));
 puts("Three controller variants: fixed labels, joined leaders, circle boundaries, unknown/invalid inputs and idempotence PASS");
 return 0;
}
