#pragma once
#include <stdint.h>
#define VBE_SCELCD_PROBE_MAGIC 0x56535039u /* VSP9 */
#define VBE_SCELCD_PROBE_VERSION 1u
#define VBE_SCELCD_PROBE_FW_365 0x03650000u
#define VBE_SCELCD_WRITER_OFFSET 0xA54u
#define VBE_SCELCD_READER_OFFSET 0x5B4u
#define VBE_SCELCD_CAPTURE_BYTES 32u
#define VBE_SCELCD_EXPECTED_BYTES 16u
typedef struct VbeSceLcdRuntimeProbe {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware;
    int32_t get_fw_ret;
    int32_t resolve_module_info_ret;
    int32_t get_lcd_ret;
    int32_t writer_offset_ret;
    int32_t reader_offset_ret;
    int32_t module_info_ret;
    int32_t reserved0;
    uint32_t lcd_modid;
    uint32_t segment_base[4];
    uint32_t segment_size[4];
    uint32_t writer_addr;
    uint32_t reader_addr;
    uint8_t writer_bytes[VBE_SCELCD_CAPTURE_BYTES];
    uint8_t reader_bytes[VBE_SCELCD_CAPTURE_BYTES];
    uint8_t expected_writer[VBE_SCELCD_EXPECTED_BYTES];
    uint8_t expected_reader[VBE_SCELCD_EXPECTED_BYTES];
} VbeSceLcdRuntimeProbe;
int vbeSceLcdRuntimeProbeGet(VbeSceLcdRuntimeProbe *out);
