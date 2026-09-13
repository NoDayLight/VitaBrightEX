#include <stdio.h>
#include "../filter_state_core.h"
#include "../display_domains.h"
static int ok(int c,const char*n){if(c)return 0;fprintf(stderr,"FAIL: %s\n",n);return 1;}
int main(void){int f=0;VbeFilterStateCore s;vbe_filter_state_init(&s);f+=ok(s.committed.cct==CCT_DEFAULT&&s.committed.invert==0&&s.committed_domains==0,"initial committed neutral");
 ScreenFilterParams p;vbe_filter_params_neutral(&p);p.invert=1;p.cct=5000;vbe_filter_state_begin_request(&s,&p,VBE_DISPLAY_DOMAIN_INVERT|VBE_DISPLAY_DOMAIN_AFFINE_CSC,VBE_DISPLAY_DOMAIN_AFFINE_CSC);
 f+=ok(s.requested.cct==5000&&s.requested.invert==1&&s.committed.cct==CCT_DEFAULT&&s.committed.invert==0,"requested is not committed");
 vbe_filter_state_commit_invert(&s,1);f+=ok(s.committed.invert==1&&s.committed.cct==CCT_DEFAULT&&s.committed_domains==VBE_DISPLAY_DOMAIN_INVERT,"commit invert only");
 vbe_filter_state_mark_failed(&s,VBE_DISPLAY_DOMAIN_AFFINE_CSC);f+=ok(s.failed_domains==VBE_DISPLAY_DOMAIN_AFFINE_CSC&&s.committed.cct==CCT_DEFAULT,"failure does not overwrite committed");
 vbe_filter_params_neutral(&p);vbe_filter_state_begin_request(&s,&p,0,0);vbe_filter_state_commit_invert(&s,0);f+=ok(s.committed_domains==0&&s.committed.invert==0,"neutral commit clears invert");
 if(f)return 1;puts("filter requested/committed state regressions: OK");return 0;}
