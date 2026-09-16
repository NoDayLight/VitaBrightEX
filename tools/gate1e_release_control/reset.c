#include "common.h"
int main(int argc,char **argv){ VbeReleaseActionBundle b={0}; int x; (void)argc;(void)argv; b.action_result=vitabrightMatrixReset(); x=vitabrightMatrixGetStatus(&b.status); if(x<0) return 10; x=vbe_release_write_file("ux0:data/vbe_release_reset.bin",&b,sizeof(b)); return x<0?11:0; }
