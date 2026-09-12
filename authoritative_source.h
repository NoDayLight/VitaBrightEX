#pragma once

/* Result of examining one candidate in an ordered authoritative-source chain.
 * Missing means the next documented candidate may be tried. Once a file was
 * opened, both success and parse/I/O failure are authoritative terminal
 * outcomes: malformed preferred input must never be hidden by a fallback. */
typedef enum {
    VBE_SOURCE_REJECT = -1,
    VBE_SOURCE_CONTINUE = 0,
    VBE_SOURCE_ACCEPT = 1,
} VbeSourceDecision;

static inline VbeSourceDecision vbe_source_decide(int opened, int result) {
    if (!opened) return VBE_SOURCE_CONTINUE;
    return result == 0 ? VBE_SOURCE_ACCEPT : VBE_SOURCE_REJECT;
}
