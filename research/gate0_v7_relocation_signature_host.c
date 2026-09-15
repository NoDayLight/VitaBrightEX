#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "diagnostics/gate0_observer/panel_relocation_signature_core.h"

static const uint8_t static_writer[16]={0x2D,0xE9,0xF8,0x43,0x42,0xF2,0x00,0x07,0xC8,0xF2,0x00,0x17,0x05,0x46,0x89,0x46};
static const uint8_t physical_writer[16]={0x2D,0xE9,0xF8,0x43,0x47,0xF2,0x00,0x07,0xC0,0xF2,0x9B,0x07,0x05,0x46,0x89,0x46};
static const uint8_t static_reader[16]={0x2D,0xE9,0xF8,0x4F,0x42,0xF2,0x00,0x06,0xC8,0xF2,0x00,0x16,0x81,0x46,0x0F,0x46};
static const uint8_t physical_reader[16]={0x2D,0xE9,0xF8,0x4F,0x47,0xF2,0x00,0x06,0xC0,0xF2,0x9B,0x06,0x81,0x46,0x0F,0x46};

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL:%s\n",#x);return 1;}}while(0)

int main(void){
    uint32_t target;
    uint8_t x[16];

    CHECK(vbe_decode_thumb_movw_movt_target(static_writer+4,static_writer+8,7,&target)&&target==0x81002000u);
    CHECK(vbe_decode_thumb_movw_movt_target(physical_writer+4,physical_writer+8,7,&target)&&target==0x009B7000u);
    CHECK(vbe_decode_thumb_movw_movt_target(static_reader+4,static_reader+8,6,&target)&&target==0x81002000u);
    CHECK(vbe_decode_thumb_movw_movt_target(physical_reader+4,physical_reader+8,6,&target)&&target==0x009B7000u);
    CHECK(vbe_relocation_normalized_signature(physical_writer,static_writer,7,0x009B7000u));
    CHECK(vbe_relocation_normalized_signature(physical_reader,static_reader,6,0x009B7000u));

    memcpy(x,physical_writer,sizeof(x));x[0]^=1u;CHECK(!vbe_relocation_normalized_signature(x,static_writer,7,0x009B7000u));
    memcpy(x,physical_reader,sizeof(x));x[15]^=1u;CHECK(!vbe_relocation_normalized_signature(x,static_reader,6,0x009B7000u));
    memcpy(x,physical_writer,sizeof(x));x[1]=0u;CHECK(!vbe_relocation_normalized_signature(x,static_writer,7,0x009B7000u));
    memcpy(x,physical_writer,sizeof(x));x[9]^=0x80u;CHECK(!vbe_relocation_normalized_signature(x,static_writer,7,0x009B7000u));
    memcpy(x,physical_writer,sizeof(x));x[7]=(uint8_t)((x[7]&0xF0u)|6u);CHECK(!vbe_relocation_normalized_signature(x,static_writer,7,0x009B7000u));
    memcpy(x,physical_writer,sizeof(x));x[11]=(uint8_t)((x[11]&0xF0u)|6u);CHECK(!vbe_relocation_normalized_signature(x,static_writer,7,0x009B7000u));
    CHECK(!vbe_relocation_normalized_signature(physical_writer,static_writer,7,0x009BE000u));
    CHECK(!vbe_relocation_normalized_signature(physical_writer,static_writer,7,0x009B7004u));
    CHECK(!vbe_relocation_normalized_signature(static_writer,static_writer,7,0x009B7000u));
    CHECK(!vbe_segment_target32((uintptr_t)0,0xD8u,&target));
    CHECK(!vbe_segment_target32((uintptr_t)0x009B7000u,0u,&target));
    CHECK(vbe_segment_target32((uintptr_t)0x009B7000u,0xD8u,&target)&&target==0x009B7000u);

    puts("GATE0_V7_RELOCATION_SIGNATURE_HOST=PASS");
    puts("writer_static_target=0x81002000 writer_physical_target=0x009B7000 rd=r7");
    puts("reader_static_target=0x81002000 reader_physical_target=0x009B7000 rd=r6");
    puts("negative_tests=PASS");
    return 0;
}
