#include "config_request_core.h"

void vbe_config_request_commit(VitaBrightConfig *live,
                               VbeSourceIdentity *live_source,
                               const VbeConfigCandidate *candidate) {
    *live = candidate->config;
    vbe_source_identity_copy(live_source, &candidate->source);
}
