#include "common.h"

int main(int argc, char **argv) {
    VbeGate1dActionBundle b = {0};
    int ret;
    (void)argc; (void)argv;
    b.action_result = vitabrightMatrixReset();
    ret = vitabrightMatrixGetStatus(&b.status);
    if (ret < 0) return 10;
    ret = gate1d_write_file("ux0:data/vbe_gate1d_reset.bin", &b, sizeof(b));
    return ret < 0 ? 11 : 0;
}
