#include <stdio.h>
#include <string.h>
#include "../config_state_core.h"
#include "../source_authority.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VitaBrightConfig live = {0};
    live.filter_cct = 6500;
    live.display_color_space_mode = 0;

    VbeSourceIdentity live_source;
    failures += ok(vbe_source_identity_file(&live_source, "ux0:A") == 0,
                   "previous file identity created");

    VbeConfigSnapshot previous;
    vbe_config_state_snapshot(&previous, &live, &live_source);

    /* Candidate FILE ur0:B was parsed, but its backend replacement fails clean.
     * The reload coordinator restores this exact snapshot. */
    live.filter_cct = 5000;
    live.display_color_space_mode = 1;
    failures += ok(vbe_source_identity_file(&live_source, "ur0:B") == 0,
                   "candidate file identity created");
    vbe_config_state_restore(&live, &live_source, &previous);
    failures += ok(live.filter_cct == 6500 && live.display_color_space_mode == 0,
                   "failed replacement restores previous config");
    failures += ok(live_source.kind == VBE_SOURCE_ID_FILE &&
                   strcmp(live_source.path, "ux0:A") == 0,
                   "failed replacement restores previous FILE source");
    failures += ok(vbe_source_identity_is_file(&live_source) &&
                   strcmp(live_source.path, "ux0:A") == 0,
                   "future authoritative-file decision still targets ux0:A");

    VbeSourceIdentity compiled;
    vbe_source_identity_compiled(&compiled);
    failures += ok(compiled.kind == VBE_SOURCE_ID_COMPILED &&
                   compiled.path[0] == '\0' &&
                   !vbe_source_identity_is_file(&compiled),
                   "both files missing can become COMPILED without fake path");

    VbeSourceOutcome malformed_preferred = vbe_source_evaluate(4, 0, -1, 0);
    failures += ok(malformed_preferred.decision == VBE_SOURCE_FAIL &&
                   malformed_preferred.stage == VBE_SOURCE_STAGE_PARSE,
                   "malformed preferred FILE is terminal, not fallback");
    failures += ok(live_source.kind == VBE_SOURCE_ID_FILE &&
                   strcmp(live_source.path, "ux0:A") == 0,
                   "malformed candidate does not alter committed source");

    if (failures) return 1;
    puts("config source identity rollback regressions: OK");
    return 0;
}
