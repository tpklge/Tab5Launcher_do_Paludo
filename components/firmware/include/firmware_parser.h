#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define FW_P4_CHIP_ID 0x12u
#define FW_MAX_FILE_SIZE (16u * 1024u * 1024u)
typedef bool (*fw_read_fn)(void *, uint32_t, void *, size_t);
typedef bool (*fw_hash_fn)(void *, uint32_t, const uint8_t expected[32]);
typedef struct {
    uint32_t image_size;
    uint16_t chip_id, min_revision, max_revision;
    uint8_t segments;
    bool hash_verified;
    char project[33], version[33], idf[33];
} firmware_image_t;
/* Strict app-only parser. No magic scanning or guessed merged offsets. */
bool firmware_parse(void *ctx, fw_read_fn read, fw_hash_fn hash, uint32_t size,
                    uint16_t chip_revision, firmware_image_t *out, char *error, size_t error_size);

/* Coprocessor parser; UINT16_MAX defers revision validation to remote bootloader. */
bool firmware_parse_chip(void *,fw_read_fn,fw_hash_fn,uint32_t,uint16_t chip,uint16_t revision,firmware_image_t *,char *,size_t);
