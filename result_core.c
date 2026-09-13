#include "result_core.h"

int vbe_result_compose(int accumulated, int stage_result) {
    if (accumulated < 0)
        return accumulated;
    if (stage_result < 0)
        return stage_result;
    if (accumulated > 0)
        return accumulated;
    return stage_result;
}
