#pragma once

#include "config.h"
#include "text_stream.h"

#define VBE_CONFIG_KEY_MAX 40
#define VBE_CONFIG_VALUE_MAX 128

typedef struct {
    VitaBrightConfig *out;
    VbeTextNewlineDecoder newline;
    char key[VBE_CONFIG_KEY_MAX];
    char value[VBE_CONFIG_VALUE_MAX];
    int key_len;
    int value_len;
    int state;
    int failed;
    int known_key;
} VbeConfigParser;

void vbe_config_defaults(VitaBrightConfig *out);
void vbe_config_parser_init(VbeConfigParser *parser, VitaBrightConfig *out);
int vbe_config_parser_feed(VbeConfigParser *parser, unsigned char raw);
int vbe_config_parser_finish(VbeConfigParser *parser);
