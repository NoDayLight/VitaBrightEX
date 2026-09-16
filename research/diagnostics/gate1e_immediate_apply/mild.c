#include <psp2/kernel/processmgr.h>
#include "action_common.h"

#define OUT_PATH "ux0:data/vbe_gate1e_immediate_mild.bin"

int main(void) {
    VbeG1eImmediateBundle b = {0};
    VbeMatrixRequestV1 r = {0};
    int write_ret;

    b.magic = VBE_G1E_IMMEDIATE_MAGIC;
    b.version = VBE_G1E_IMMEDIATE_VERSION;
    b.action_kind = VBE_G1E_IMMEDIATE_ACTION_MILD;
    b.before_status_return = vitabrightMatrixGetStatus(&b.before);

    r.size = sizeof(r);
    r.version = VBE_MATRIX_REQUEST_VERSION;
    r.hardware_component_s3_9[0] = 461;
    r.hardware_component_s3_9[4] = 512;
    r.hardware_component_s3_9[8] = 512;
    b.action_result = vitabrightMatrixSetRequest(&r);
    b.published_status_return = vitabrightMatrixGetStatus(&b.published);

    if (b.before_status_return == 0 && b.action_result >= 0 && b.published_status_return == 0)
        b.replay_return = vbeG1eImmediateReplayCanonical();
    else
        b.replay_return = VBE_G1E_SKIPPED_RETURN;

    b.observer_status_return = vbeG1eImmediateGetStatus(&b.observer);
    b.after_status_return = vitabrightMatrixGetStatus(&b.after);
    write_ret = gate1e_write_file(OUT_PATH, &b, sizeof(b));
    sceKernelExitProcess(write_ret < 0 ? 1 : 0);
    return 0;
}
