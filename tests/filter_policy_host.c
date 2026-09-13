#include <stdio.h>
#include "../filter_policy.h"
#include "../filter_state_core.h"

static int ok(int c,const char*n){if(c)return 0;fprintf(stderr,"FAIL: %s\n",n);return 1;}
int main(void){int f=0;ScreenFilterParams p;vbe_filter_params_neutral(&p);VbeFilterRequestPolicy q=vbe_filter_request_policy(&p);
 f+=ok(q.requested_domains==0&&q.unsupported_domains==0,"neutral");
 p.invert=1;q=vbe_filter_request_policy(&p);f+=ok(q.requested_domains==VBE_DISPLAY_DOMAIN_INVERT&&q.attempt_domains==VBE_DISPLAY_DOMAIN_INVERT&&q.unsupported_domains==0,"invert independent");
 vbe_filter_params_neutral(&p);p.cct=5000;q=vbe_filter_request_policy(&p);f+=ok((q.requested_domains&VBE_DISPLAY_DOMAIN_AFFINE_CSC)!=0&&(q.unsupported_domains&VBE_DISPLAY_DOMAIN_AFFINE_CSC)!=0&&q.csc_state==VBE_CAP_UNSUPPORTED,"CCT affine unsupported only");
 vbe_filter_params_neutral(&p);p.gamma=1.2f;q=vbe_filter_request_policy(&p);f+=ok((q.requested_domains&VBE_DISPLAY_DOMAIN_TRANSFER)!=0&&(q.unsupported_domains&VBE_DISPLAY_DOMAIN_TRANSFER)!=0&&q.transfer_state==VBE_CAP_UNSUPPORTED,"gamma transfer unsupported only");
 vbe_filter_params_neutral(&p);p.invert=1;p.cct=5000;q=vbe_filter_request_policy(&p);f+=ok((q.attempt_domains&VBE_DISPLAY_DOMAIN_INVERT)!=0&&(q.unsupported_domains&VBE_DISPLAY_DOMAIN_AFFINE_CSC)!=0,"unsupported affine does not suppress invert");
 if(f)return 1;puts("filter capability decomposition regressions: OK");return 0;}
