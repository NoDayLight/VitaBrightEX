#include "config_state_core.h"

void vbe_config_state_snapshot(VbeConfigSnapshot *out,
                               const VitaBrightConfig *config,
                               const VbeSourceIdentity *source) {
    out->config = *config;
    vbe_source_identity_copy(&out->source, source);
}

void vbe_config_state_restore(VitaBrightConfig *config,
                              VbeSourceIdentity *source,
                              const VbeConfigSnapshot *snapshot) {
    *config = snapshot->config;
    vbe_source_identity_copy(source, &snapshot->source);
}
