#pragma once
#include <stdint.h>
#include <stdbool.h>
#define BOOT_REQUEST_OFFSET 0x12000u
#define BOOT_REQUEST_SIZE 0x1000u
#define BOOT_CAP_OFFSET 64u
#define BOOT_CAP_MAGIC 0x3150434Cu
#define BOOT_REQUEST_MAGIC 0x3152514Cu
typedef struct { uint32_t magic,slot,address,size,crc; } boot_request_t;
static inline uint32_t boot_request_crc(const boot_request_t *r)
{
    const uint8_t *p=(const uint8_t*)r; uint32_t crc=0xFFFFFFFFu;
    for (unsigned i=0;i<16;i++) { crc^=p[i]; for (unsigned j=0;j<8;j++) crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1))); }
    return ~crc;
}
static inline bool boot_request_valid(const boot_request_t *r)
{
    return r->magic==BOOT_REQUEST_MAGIC && r->slot<16 && r->address && r->size && r->crc==boot_request_crc(r);
}

/* Reserved, unpartitioned gap between launch_req and factory launcher. */
#define LAYOUT_CAP_OFFSET 68u
#define LAYOUT_CAP_MAGIC 0x3254594Cu
#define LAYOUT_MIRROR_OFFSET 0x13000u
#define LAYOUT_TX_OFFSET 0x14000u
#define LAYOUT_TX_MAGIC 0x3258544Cu
typedef struct { uint32_t magic,old_crc,new_crc,crc; } layout_recovery_t;
static inline uint32_t boot_crc_bytes(const void *bytes,unsigned size)
{
 const uint8_t *p=bytes; uint32_t crc=0xffffffffu;
 for(unsigned i=0;i<size;i++) { crc^=p[i]; for(unsigned j=0;j<8;j++) crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1))); }
 return ~crc;
}
static inline bool layout_recovery_valid(const layout_recovery_t *r)
{
 return r->magic==LAYOUT_TX_MAGIC && r->crc==boot_crc_bytes(&r->old_crc,8);
}
