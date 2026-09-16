#include "common.h"
int main(int argc,char **argv){
    VbeMatrixRequestV1 r={0}; VbeReleaseActionBundle b={0}; int x; (void)argc;(void)argv;
    r.size=sizeof(r); r.version=VBE_MATRIX_REQUEST_VERSION; r.hardware_component_s3_9[0]=461; r.hardware_component_s3_9[4]=512; r.hardware_component_s3_9[8]=512;
    b.action_result=vitabrightMatrixSetRequest(&r); x=vitabrightMatrixGetStatus(&b.status); if(x<0) return 10;
    x=vbe_release_write_file("ux0:data/vbe_release_mild.bin",&b,sizeof(b)); return x<0?11:0;
}
