#include "text_stream.h"

void vbe_text_newline_init(VbeTextNewlineDecoder *decoder) {
    decoder->pending_cr = 0;
}

int vbe_text_newline_feed(VbeTextNewlineDecoder *decoder,
                          unsigned char raw,
                          unsigned char *logical) {
    if (decoder->pending_cr) {
        if (raw != '\n') return -1;
        decoder->pending_cr = 0;
        *logical = '\n';
        return 1;
    }

    if (raw == '\r') {
        decoder->pending_cr = 1;
        return 0;
    }

    *logical = raw;
    return 1;
}

int vbe_text_newline_finish(const VbeTextNewlineDecoder *decoder) {
    return decoder->pending_cr ? -1 : 0;
}
