#include <stdio.h>
#include <string.h>
#include "../config_request_core.h"
#include "../transaction_core.h"

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
                   "live FILE A identity created");

    VbeConfigCandidate candidate = {0};
    candidate.config = live;
    candidate.config.filter_cct = 5000;
    candidate.config.display_color_space_mode = 1;
    failures += ok(vbe_source_identity_file(&candidate.source, "ur0:B") == 0,
                   "candidate FILE B identity created");

    failures += ok(live.filter_cct == 6500 &&
                   live.display_color_space_mode == 0 &&
                   strcmp(live_source.path, "ux0:A") == 0,
                   "candidate construction leaves accepted request untouched");

    vbe_config_request_commit(&live, &live_source, &candidate);
    failures += ok(live.filter_cct == 5000 &&
                   live.display_color_space_mode == 1 &&
                   live_source.kind == VBE_SOURCE_ID_FILE &&
                   strcmp(live_source.path, "ur0:B") == 0,
                   "candidate commit accepts FILE B request and provenance");

    /* A clean backend replacement failure/rollback is independent of config
     * request provenance. The hardware can remain on source A while the
     * accepted request remains FILE B for future reconciliation. */
    VbeSourceIdentity oled_committed;
    failures += ok(vbe_source_identity_file(&oled_committed, "ux0:oled-A") == 0,
                   "previous OLED source created");
    VbeTxnAttempt requested = vbe_txn_failed_clean(17, -17);
    VbeTxnAttempt rollback = vbe_txn_ok();
    failures += ok(vbe_txn_public_result(requested, 1, rollback) < 0,
                   "failed-clean replacement still reports requested failure");
    failures += ok(strcmp(live_source.path, "ur0:B") == 0 &&
                   strcmp(oled_committed.path, "ux0:oled-A") == 0,
                   "backend rollback cannot revert accepted config request");

    VbeSourceOutcome malformed = vbe_source_evaluate(4, 0, -1, 0);
    failures += ok(malformed.decision == VBE_SOURCE_FAIL &&
                   malformed.stage == VBE_SOURCE_STAGE_PARSE,
                   "malformed preferred FILE is terminal, not fallback");
    failures += ok(strcmp(live_source.path, "ur0:B") == 0,
                   "rejected malformed candidate cannot alter accepted request");

    VbeConfigCandidate compiled = {0};
    vbe_source_identity_compiled(&compiled.source);
    failures += ok(compiled.source.kind == VBE_SOURCE_ID_COMPILED &&
                   compiled.source.path[0] == '\0' &&
                   !vbe_source_identity_is_file(&compiled.source),
                   "both missing sources can yield COMPILED without fake path");

    if (failures) return 1;
    puts("config request-state regressions: OK");
    return 0;
}
