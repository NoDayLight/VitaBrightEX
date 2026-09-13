#pragma once
#include "config.h"

void vbe_config_request_commit(VitaBrightConfig *live,
                               VbeSourceIdentity *live_source,
                               const VbeConfigCandidate *candidate);
