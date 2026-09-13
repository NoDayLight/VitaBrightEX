#include <stdio.h>
#include <string.h>
#include "../affine_core.h"
static int ok(int c,const char*n){if(c)return 0;fprintf(stderr,"FAIL: %s\n",n);return 1;}
int main(void){int f=0;VbeAffineTransform id,t,u,a,b;vbe_affine_identity(&id);f+=ok(vbe_affine_is_identity(&id),"identity constructor");
 f+=ok(vbe_affine_cct(&t,6500)==0&&vbe_affine_is_identity(&t),"6500 exact identity");f+=ok(vbe_affine_contrast(&t,VBE_AFFINE_ONE,VBE_AFFINE_ONE/2)==0&&vbe_affine_is_identity(&t),"contrast 1 exact identity");f+=ok(vbe_affine_brightness(&t,0)==0&&vbe_affine_is_identity(&t),"brightness 0 exact identity");
 VbeAffineRequest req={6500,VBE_AFFINE_ONE,0,VBE_AFFINE_ONE/2};f+=ok(vbe_affine_build(&t,&req)==0&&vbe_affine_is_identity(&t),"all-neutral build exact identity");
 f+=ok(vbe_affine_cct(&t,3000)==0&&t.m[2][2]<VBE_AFFINE_ONE,"warm reduces blue");f+=ok(vbe_affine_cct(&t,10000)==0&&t.m[0][0]<VBE_AFFINE_ONE,"cool reduces red");
 f+=ok(vbe_affine_cct(&t,6499)==0&&vbe_affine_cct(&u,6501)==0&&t.m[0][0]>=u.m[0][0],"CCT interpolation continuous around neutral");
 f+=ok(vbe_affine_compose(&a,&id,&t)==0&&memcmp(&a,&t,sizeof(a))==0,"identity after composition stable");f+=ok(vbe_affine_compose(&b,&t,&id)==0&&memcmp(&b,&t,sizeof(b))==0,"identity before composition stable");
 req.kelvin=1000;req.contrast_q16=4*VBE_AFFINE_ONE;req.brightness_q16=VBE_AFFINE_ONE;req.pivot_q16=VBE_AFFINE_ONE;f+=ok(vbe_affine_build(&t,&req)==0,"warm legal extreme safe");req.kelvin=25100;req.brightness_q16=-VBE_AFFINE_ONE;f+=ok(vbe_affine_build(&t,&req)==0,"cool legal extreme safe");
 f+=ok(vbe_affine_cct(&t,999)<0&&vbe_affine_contrast(&t,4*VBE_AFFINE_ONE+1,0)<0&&vbe_affine_brightness(&t,VBE_AFFINE_ONE+1)<0,"illegal extremes rejected");
 if(f)return 1;puts("affine transform core regressions: OK");return 0;}
