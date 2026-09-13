/*
 * Gate-0 drain/controller extension.
 *
 * Keep the audited v3 drain implementation as an include so its I/O,
 * quiescence, stable-snapshot and record-validation mechanics remain exactly
 * the pending-tree implementation. Rename only its entry point and layer the
 * v4 authority classification on top.
 */
#define main vbeTraceLegacyDrainMain
#include "drain_core.inc"
#undef main

#define VALIDATION_AFFINE_AUTHORITY (1u << 4)
#define VALIDATION_PANEL_AUTHORITY  (1u << 5)

static uint32_t annotate_panel_read_validity(VbeTraceRecord *records,
                                             uint32_t count) {
    uint32_t i, saw_proven = 0u, saw_uncertain = 0u;
    for (i = 0; i < count; ++i) {
        VbeTraceRecord *r = &records[i];
        if (r->event_type != VBE_TRACE_PANEL_READ_EXIT || r->result < 0 ||
            (r->flags & VBE_TRACE_FLAG_NULL) != 0u)
            continue;

        /*
         * Retail-3.65 static evidence closes only the naturally-observed
         * GET_POWER_MODE-like read: command 0x0A, requested length 1. The
         * caller tests return >= 0 and consumes that one byte. The private
         * reader's return is a status code, not a transfer count. Until the
         * arbitrary-length initialization contract is proven, all other
         * successful read payloads remain diagnostic-only/uncertain.
         */
        if (r->arg0 == 0x0Au && r->arg1 == 1u && r->result == 0 &&
            r->payload_length == 1u &&
            (r->flags & VBE_TRACE_FLAG_TRUNCATED) == 0u) {
            r->flags |= VBE_TRACE_FLAG_READ_PAYLOAD_PROVEN;
            saw_proven = 1u;
        } else {
            r->flags |= VBE_TRACE_FLAG_READ_PAYLOAD_UNCERTAIN;
            saw_uncertain = 1u;
        }
    }
    if (saw_uncertain) return VBE_TRACE_PANEL_READ_HAS_UNCERTAIN;
    return saw_proven ? VBE_TRACE_PANEL_READ_PROVEN_ONLY :
                        VBE_TRACE_PANEL_READ_NONE;
}

int main(void) {
    VbeTraceStatus status;
    VbeTraceSnapshot pre_snapshot, post_snapshot;
    VbeTraceDumpHeader header;
    uint32_t record_count = 0u;
    uint32_t post_retry_count = 0u;
    uint32_t truncated_seen = 0u;
    uint32_t common_snapshot_mask;
    uint32_t read_validity;
    int pre_present, ret, reset_ret;

    ret = vbeTraceStop();
    if (ret < 0) return ret;
    ret = wait_quiescent(&status);
    if (ret < 0) return ret;
    ret = stable_snapshot(&post_snapshot, &post_retry_count);
    if (ret < 0) return ret;
    ret = vbeTraceRead(g_records, VBE_TRACE_RECORD_CAPACITY, &record_count);
    if (ret < 0) return ret;
    ret = vbeTraceGetStatus(&status);
    if (ret < 0) return ret;
    ret = validate_records(g_records, record_count, &status, &truncated_seen);
    if (ret < 0) return ret;

    pre_present = load_boundary(&pre_snapshot);
    common_snapshot_mask = pre_present ?
        (pre_snapshot.available_mask & post_snapshot.available_mask) : 0u;
    read_validity = annotate_panel_read_validity(g_records, record_count);

    header.magic = VBE_TRACE_DUMP_MAGIC;
    header.version = VBE_TRACE_VERSION;
    header.header_size = (uint32_t)sizeof(header);
    header.status_size = (uint32_t)sizeof(status);
    header.pre_snapshot_size = pre_present ? (uint32_t)sizeof(pre_snapshot) : 0u;
    header.post_snapshot_size = (uint32_t)sizeof(post_snapshot);
    header.record_size = (uint32_t)sizeof(VbeTraceRecord);
    header.record_count = record_count;
    header.pre_snapshot_present = pre_present ? 1u : 0u;
    header.pre_snapshot_retry_count = 0u;
    header.post_snapshot_retry_count = post_retry_count;

    header.affine_required_mask = VBE_TRACE_REQUIRED_AFFINE_HOOKS;
    header.affine_missing_required_mask =
        VBE_TRACE_REQUIRED_AFFINE_HOOKS & ~status.installed_hook_mask;
    header.panel_required_mask = VBE_TRACE_REQUIRED_PANEL_HOOKS;
    header.panel_missing_required_mask =
        VBE_TRACE_REQUIRED_PANEL_HOOKS & ~status.installed_hook_mask;

    header.affine_required_snapshot_mask =
        VBE_TRACE_REQUIRED_AFFINE_SNAPSHOTS;
    header.affine_missing_required_snapshot_mask =
        VBE_TRACE_REQUIRED_AFFINE_SNAPSHOTS & ~common_snapshot_mask;
    header.panel_required_snapshot_mask =
        VBE_TRACE_REQUIRED_PANEL_SNAPSHOTS;
    header.panel_missing_required_snapshot_mask =
        VBE_TRACE_REQUIRED_PANEL_SNAPSHOTS & ~common_snapshot_mask;

    header.affine_capture_quality =
        (pre_present && header.affine_missing_required_mask == 0u &&
         header.affine_missing_required_snapshot_mask == 0u &&
         status.lost_records == 0u) ?
        VBE_TRACE_CAPTURE_AUTHORITATIVE : VBE_TRACE_CAPTURE_PARTIAL;

    header.panel_capture_quality =
        (pre_present && header.panel_missing_required_mask == 0u &&
         header.panel_missing_required_snapshot_mask == 0u &&
         status.lost_records == 0u && truncated_seen == 0u &&
         read_validity != VBE_TRACE_PANEL_READ_HAS_UNCERTAIN) ?
        VBE_TRACE_CAPTURE_AUTHORITATIVE : VBE_TRACE_CAPTURE_PARTIAL;

    /* Existing consumers see the stricter aggregate quality. */
    header.capture_quality = header.panel_capture_quality;
    header.panel_read_validity = read_validity;
    header.validation_flags = VALIDATION_RECORDS_OK | VALIDATION_POST_STABLE |
        (pre_present ? VALIDATION_PRE_PRESENT : 0u) |
        (truncated_seen ? 0u : VALIDATION_NO_TRUNCATION) |
        (header.affine_capture_quality == VBE_TRACE_CAPTURE_AUTHORITATIVE ?
            VALIDATION_AFFINE_AUTHORITY : 0u) |
        (header.panel_capture_quality == VBE_TRACE_CAPTURE_AUTHORITATIVE ?
            VALIDATION_PANEL_AUTHORITY : 0u);
    header.reserved[0] = 0u;
    header.reserved[1] = 0u;
    header.reserved[2] = 0u;

    ret = write_dump(&header, &status, &pre_snapshot, &post_snapshot, g_records);
    if (ret >= 0) ret = save_boundary(&post_snapshot);
    reset_ret = reset_and_enable();
    if (ret >= 0 && reset_ret < 0) ret = reset_ret;
    return ret;
}
