#include "common.h"

int main(int argc, char **argv) {
    VbeMatrixRequestV1 r = {0};
    VbeGate1dActionBundle b = {0};
    int ret;
    (void)argc; (void)argv;
    r.size = sizeof(r);
    r.version = VBE_MATRIX_REQUEST_VERSION;
    r.hardware_component_s3_9[0] = 461;
    r.hardware_component_s3_9[4] = 512;
    r.hardware_component_s3_9[8] = 512;
    b.action_result = vitabrightMatrixSetRequest(&r);
    ret = vitabrightMatrixGetStatus(&b.status);
    if (ret < 0) return 10;
    ret = gate1d_write_file("ux0:data/vbe_gate1d_mild.bin", &b, sizeof(b));
    return ret < 0 ? 11 : 0;
}
