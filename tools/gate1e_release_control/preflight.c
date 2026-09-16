#include "common.h"
int main(int argc,char **argv){
    VbeReleasePreflightBundle b={0}; int r; (void)argc;(void)argv;
    r=vitabrightGetBuildId(b.build_id); if(r<0) return 10;
    r=vitabrightMatrixGetCapabilities(&b.capabilities); if(r<0) return 11;
    r=vitabrightMatrixGetStatus(&b.status); if(r<0) return 12;
    r=vbe_release_write_file("ux0:data/vbe_release_preflight.bin",&b,sizeof(b)); return r<0?13:0;
}
