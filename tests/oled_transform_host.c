#include <stdio.h>
#include <string.h>
#include "../oled/transform_core.h"
static int ok(int c,const char*n){if(c)return 0;fprintf(stderr,"FAIL: %s\n",n);return 1;}
static int channel_slot(int col,int channel){return col%3==channel;}
int main(void){int f=0;unsigned char base[LUT_SIZE],out[LUT_SIZE],again[LUT_SIZE],before[LUT_SIZE];for(int i=0;i<LUT_SIZE;++i)base[i]=(unsigned char)((i*37+11)&255);memcpy(before,base,LUT_SIZE);VbeOledTransformParams p;vbe_oled_transform_neutral(&p);
 f+=ok(vbe_oled_transform_lut(base,out,&p)==0&&memcmp(base,out,LUT_SIZE)==0,"neutral exact 357 bytes");f+=ok(memcmp(base,before,LUT_SIZE)==0,"base immutable");
 p.bias.r_offset=7;f+=ok(vbe_oled_transform_lut(base,out,&p)==0,"R apply");for(int row=0;row<LUT_ROWS;++row)for(int col=0;col<LUT_LINE_SIZE;++col){int idx=row*LUT_LINE_SIZE+col;int expected=base[idx]+(channel_slot(col,0)?7:0);if(expected>255)expected=255;f+=ok(out[idx]==(unsigned char)expected,"R touches only 0,3,..18");}
 vbe_oled_transform_neutral(&p);p.bias.g_offset=-9;f+=ok(vbe_oled_transform_lut(base,out,&p)==0,"G apply");for(int row=0;row<LUT_ROWS;++row)for(int col=0;col<LUT_LINE_SIZE;++col){int idx=row*LUT_LINE_SIZE+col;int expected=base[idx]-(channel_slot(col,1)?9:0);if(expected<0)expected=0;f+=ok(out[idx]==(unsigned char)expected,"G touches only 1,4,..19");}
 vbe_oled_transform_neutral(&p);p.bias.b_offset=300;f+=ok(vbe_oled_transform_lut(base,out,&p)==0,"B saturating apply");for(int row=0;row<LUT_ROWS;++row)for(int col=0;col<LUT_LINE_SIZE;++col){int idx=row*LUT_LINE_SIZE+col;if(channel_slot(col,2))f+=ok(out[idx]==255,"B saturation bounded");}
 vbe_oled_transform_neutral(&p);p.bias.r_offset=3;p.warm.enabled=1;p.warm.first_row=12;p.warm.g_offset=-4;p.warm.b_offset=-8;f+=ok(vbe_oled_transform_lut(base,out,&p)==0&&vbe_oled_transform_lut(base,again,&p)==0&&memcmp(out,again,LUT_SIZE)==0,"repeated apply derives from base, no compounding");f+=ok(memcmp(base,before,LUT_SIZE)==0,"runtime transform never mutates base");
 if(f)return 1;puts("OLED register-domain transform regressions: OK");return 0;}
