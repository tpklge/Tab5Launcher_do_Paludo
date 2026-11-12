#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "bootloader_utility.h"
#include "boot_request.h"
static unsigned char flash[0x20000];
static bool erase_fails,table_fails;
static int selected;
static jmp_buf jump;
bool bootloader_utility_load_partition_table(bootloader_state_t *s) {
    s->factory=(pos_t){0x20000,0x1f0000}; s->ota[0]=(pos_t){0x210000,0x800000}; return !table_fails;
}
void bootloader_utility_load_boot_image(const bootloader_state_t *s,int index) { selected=index; longjmp(jump,1); }
int bootloader_flash_read(size_t offset,void *out,size_t size,bool decrypt) {
    if(offset>sizeof(flash) || size>sizeof(flash)-offset) return ESP_FAIL;
    memcpy(out,flash+offset,size); return ESP_OK;
}
int bootloader_flash_write(size_t offset,void *in,size_t size,bool encrypted) { memcpy(flash+offset,in,size); return ESP_OK; }
int bootloader_flash_erase_range(uint32_t offset,uint32_t size) {
    if(erase_fails) return ESP_FAIL; memset(flash+offset,0xff,size); return ESP_OK;
}
void bootloader_after_init(void);
static void boot(void) { selected=-99; if(setjmp(jump)==0) bootloader_after_init(); }
static void request(void) {
    boot_request_t r={BOOT_REQUEST_MAGIC,0,0x210000,0x800000,0}; r.crc=boot_request_crc(&r);
    memcpy(flash+BOOT_REQUEST_OFFSET,&r,sizeof(r));
}
int main(void) {
    memset(flash,0xff,sizeof(flash));
    esp_partition_info_t p={.magic=ESP_PARTITION_MAGIC,.type=1,.subtype=0x40,.pos={BOOT_REQUEST_OFFSET,BOOT_REQUEST_SIZE},.label="launch_req"};
    memcpy(flash+ESP_PARTITION_TABLE_OFFSET,&p,sizeof(p));
    boot(); assert(selected==FACTORY_INDEX);
    uint32_t capability; memcpy(&capability,flash+BOOT_REQUEST_OFFSET+BOOT_CAP_OFFSET,4); assert(capability==BOOT_CAP_MAGIC);
    request(); boot(); assert(selected==0);
    boot(); assert(selected==FACTORY_INDEX); /* consumed request can't repeat, even on power cycle */
    request(); flash[BOOT_REQUEST_OFFSET+4]=16; boot(); assert(selected==FACTORY_INDEX);
    request(); flash[BOOT_REQUEST_OFFSET+8]^=1; boot(); assert(selected==FACTORY_INDEX);
    request(); erase_fails=true; boot(); assert(selected==FACTORY_INDEX); erase_fails=false; boot(); assert(selected==FACTORY_INDEX);
    memset(flash+ESP_PARTITION_TABLE_OFFSET,0xff,32); request(); boot(); assert(selected==FACTORY_INDEX);
    table_fails=true; boot(); assert(selected==-99); /* leave standard recovery to SDK */
    table_fails=false; memset(flash+ESP_PARTITION_TABLE_OFFSET,255,4096); memcpy(flash+ESP_PARTITION_TABLE_OFFSET,&p,sizeof(p));
    memset(flash+BOOT_REQUEST_OFFSET,255,4096);
    unsigned char old_table[4096]; memcpy(old_table,flash+ESP_PARTITION_TABLE_OFFSET,4096);
    memcpy(flash+LAYOUT_MIRROR_OFFSET,old_table,4096);
    layout_recovery_t tx={.magic=LAYOUT_TX_MAGIC,.old_crc=boot_crc_bytes(old_table,4096)};
    old_table[1000]=0x42; tx.new_crc=boot_crc_bytes(old_table,4096); old_table[1000]=255; tx.crc=boot_crc_bytes(&tx.old_crc,8);
    memcpy(flash+LAYOUT_TX_OFFSET,&tx,sizeof(tx)); memset(flash+ESP_PARTITION_TABLE_OFFSET,0,64);
    boot(); assert(selected==FACTORY_INDEX && memcmp(flash+ESP_PARTITION_TABLE_OFFSET,old_table,4096)==0);
    assert(!layout_recovery_valid((layout_recovery_t*)(flash+LAYOUT_TX_OFFSET)));
    memcpy(flash+LAYOUT_TX_OFFSET,&tx,sizeof(tx)); flash[ESP_PARTITION_TABLE_OFFSET+1000]=0x42;
    boot(); assert(selected==FACTORY_INDEX && flash[ESP_PARTITION_TABLE_OFFSET+1000]==0x42);
    memcpy(flash+LAYOUT_TX_OFFSET,&tx,sizeof(tx)); memset(flash+ESP_PARTITION_TABLE_OFFSET,0,64); erase_fails=true;
    boot(); assert(selected==-99 && layout_recovery_valid((layout_recovery_t*)(flash+LAYOUT_TX_OFFSET)));
    erase_fails=false; boot(); assert(selected==FACTORY_INDEX && memcmp(flash+ESP_PARTITION_TABLE_OFFSET,old_table,4096)==0);
    memcpy(flash+LAYOUT_TX_OFFSET,&tx,sizeof(tx)); memset(flash+ESP_PARTITION_TABLE_OFFSET,0,64); flash[LAYOUT_MIRROR_OFFSET]^=1;
    boot(); assert(selected==-99); /* Never restore an unverified mirror. */
    puts("Boot policy: one-shot, reset return, corrupt/partial requests, missing partition and erase failure passed (ASan/UBSan).");
}
