from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
msc=(r/'components/espressif__usb_host_msc/src/msc_host.c').read_text()
bulk=msc[msc.index('esp_err_t msc_bulk_transfer('):msc.index('esp_err_t msc_control_transfer(')]
disk=(r/'components/espressif__usb_host_msc/src/diskio_usb.c').read_text()
io=disk[disk.index('static DRESULT usb_disk_read('):disk.index('static DRESULT usb_disk_ioctl(')]
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_MSC_STALL 4
#define ESP_ERR_MSC_INTERNAL 5
#define MSC_RETURN_ON_ERROR(call) do { int err=(call); if(err) return err; } while(0)
#define ESP_LOGE(...) ((void)0)
#define USB_TRANSFER_STATUS_COMPLETED 0
#define USB_TRANSFER_STATUS_STALL 1
typedef int usb_transfer_status_t;
typedef struct {size_t data_buffer_size; uint8_t *data_buffer; unsigned bEndpointAddress; size_t num_bytes, actual_num_bytes;void *device_handle,*context;void (*callback)(void*);unsigned timeout_ms;} usb_transfer_t;
typedef struct {uint32_t block_size,block_count;} usb_disk_t;
typedef struct {usb_transfer_t *xfer;void *handle;struct {unsigned bulk_in_mps,bulk_in_ep,bulk_out_ep;} config;usb_disk_t disk;} msc_device_t;
typedef int msc_endpoint_t;
#define MSC_EP_IN 1
#define MSC_EP_OUT 0
static int fail_alloc,fail_free,allocs,frees;
static int usb_host_transfer_alloc(size_t n,int flags,usb_transfer_t **out) {if(fail_alloc)return ESP_ERR_NO_MEM;*out=calloc(1,sizeof(**out));(*out)->data_buffer=calloc(1,n);(*out)->data_buffer_size=n;allocs++;return 0;}
static int usb_host_transfer_free(usb_transfer_t *p) {if(fail_free){fail_free--;return ESP_FAIL;}free(p->data_buffer);free(p);frees++;return 0;}
static int usb_host_transfer_submit(usb_transfer_t *p) {p->actual_num_bytes=p->num_bytes;return 0;}
static int wait_for_transfer_done(usb_transfer_t *p) {return 0;}
static void transfer_callback(void *p) {}
static size_t usb_round_up_to_mps(size_t n,unsigned mps) {return (n+mps-1)/mps*mps;}
'''+bulk+r'''
typedef unsigned char BYTE;
typedef uint32_t DWORD;
typedef unsigned UINT;
typedef int DRESULT;
#define FF_VOLUMES 2
#define RES_OK 0
#define RES_ERROR 1
#define RES_PARERR 2
#define __containerof(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
static usb_disk_t *s_disks[FF_VOLUMES];
static unsigned calls,expected_sector,expected_count,sector_bytes,fail_call;
static const uint8_t *cursor;
static int command(msc_device_t *d,const uint8_t *b,unsigned sector,unsigned count,unsigned size) {
 assert(size==sector_bytes && count && count*size<=4096);
 assert(sector==expected_sector && b==cursor);
 calls++;if(calls==fail_call)return ESP_FAIL;
 expected_sector+=count;expected_count-=count;cursor+=count*size;return 0;
}
#define scsi_cmd_read10 command
#define scsi_cmd_write10 command
'''+io+r'''
int main(void) {
 msc_device_t d={.config={64,1,2}};
 assert(!usb_host_transfer_alloc(64,0,&d.xfer));usb_transfer_t *old=d.xfer;
 uint8_t bytes[65536]={0};
 fail_alloc=1;assert(msc_bulk_transfer(&d,bytes,32768,MSC_EP_OUT)==ESP_ERR_NO_MEM);
 assert(d.xfer==old && frees==0);
 // REQUEST SENSE/CBW after the failed large allocation still uses a live buffer.
 assert(!msc_bulk_transfer(&d,bytes,31,MSC_EP_OUT));
 fail_alloc=0;assert(!msc_bulk_transfer(&d,bytes,4096,MSC_EP_OUT));assert(frees==1);
 // Failed free keeps the original pointer and cleans up the replacement.
 old=d.xfer;fail_free=1;
 assert(msc_bulk_transfer(&d,bytes,8192,MSC_EP_OUT)==ESP_FAIL);
 assert(d.xfer==old && d.xfer->data_buffer_size==4096);
 assert(!msc_bulk_transfer(&d,bytes,31,MSC_EP_OUT));
 assert(!usb_host_transfer_free(d.xfer));assert(allocs==frees);
 for(unsigned ss=512;ss<=4096;ss*=2) {
  d.disk.block_size=ss;s_disks[0]=&d.disk;sector_bytes=ss;
  for(int write=0;write<2;write++) {
   calls=0;fail_call=0;expected_sector=42;expected_count=65536/ss;cursor=bytes;
   int err=write?usb_disk_write(0,bytes,42,expected_count):usb_disk_read(0,bytes,42,expected_count);
   assert(!err && !expected_count && calls==16);
   calls=0;expected_sector=42;expected_count=65536/ss-1;cursor=bytes;
   err=write?usb_disk_write(0,bytes,42,expected_count):usb_disk_read(0,bytes,42,expected_count);
   assert(!err && !expected_count && calls==(65536-ss+4095)/4096);
   calls=0;fail_call=2;expected_sector=42;expected_count=65536/ss;cursor=bytes;
   err=write?usb_disk_write(0,bytes,42,expected_count):usb_disk_read(0,bytes,42,expected_count);
   assert(err==RES_ERROR && calls==2 && expected_count==65536/ss-4096/ss);
   calls=0;fail_call=0;expected_sector=42;expected_count=1;cursor=bytes;
   assert(!(write?usb_disk_write(0,bytes,42,1):usb_disk_read(0,bytes,42,1)));
   assert(calls==1 && expected_count==0);
  }
 }
 puts("USB MSC: failed DMA growth retains live transfer, recovery reuse, bounded 64KiB cluster read/write, sector sizes 512..4096 and error stop passed (ASan/UBSan).");
}
'''
env=os.environ.copy();sdk='/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk'
if Path(sdk).exists():env.setdefault('SDKROOT',sdk)
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],env=env,check=True)
 subprocess.run([str(p/'test')],env=env,check=True)
