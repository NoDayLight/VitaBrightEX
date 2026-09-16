#include "common.h"

int main(int argc, char **argv) {
    VbeGate1eStatusBundle b = {0};
    int ret;
    (void)argc; (void)argv;
    ret = vitabrightMatrixGetCapabilities(&b.capabilities);
    if (ret < 0) return 10;
    ret = vitabrightMatrixGetStatus(&b.status);
    if (ret < 0) return 11;
    ret = gate1e_write_file("ux0:data/vbe_gate1e_integrated_status.bin", &b, sizeof(b));
    return ret < 0 ? 12 : 0;
}
