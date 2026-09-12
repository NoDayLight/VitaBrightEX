#pragma once

typedef struct {
    int pending_cr;
} VbeTextNewlineDecoder;

void vbe_text_newline_init(VbeTextNewlineDecoder *decoder);
int vbe_text_newline_feed(VbeTextNewlineDecoder *decoder, unsigned char raw, unsigned char *logical);
int vbe_text_newline_finish(const VbeTextNewlineDecoder *decoder);
