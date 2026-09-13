#pragma once
#include "config.h"

void vbe_config_state_snapshot(VbeConfigSnapshot *out,
                               const VitaBrightConfig *config,
                               const VbeSourceIdentity *source);
void vbe_config_state_restore(VitaBrightConfig *config,
                              VbeSourceIdentity *source,
                              const VbeConfigSnapshot *snapshot);
