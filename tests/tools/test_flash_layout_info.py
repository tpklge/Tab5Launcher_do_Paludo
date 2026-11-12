from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2];env=os.environ.copy();sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
if sdk.exists():env.setdefault('SDKROOT',str(sdk))
with tempfile.TemporaryDirectory() as t:
 p=Path(t)
 (p/'esp_partition.h').write_text('''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t address,size;} esp_partition_t;
typedef esp_partition_t* esp_partition_iterator_t;
#define ESP_PARTITION_TYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_ANY 255
static esp_partition_t rows[16];static unsigned count;
static inline esp_partition_iterator_t esp_partition_find(int a,int b,const char*c){return count?rows:NULL;}
static inline const esp_partition_t *esp_partition_get(esp_partition_iterator_t p){return p;}
static inline esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t p){return p+1<rows+count?p+1:NULL;}
''')
 (p/'test.c').write_text('''#include "flash_layout_info.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 assert(flash_layout_tail_allocated()==0);
 rows[0]=(esp_partition_t){0x20000,0x1f0000};rows[1]=(esp_partition_t){0xff0000,0x10000};count=2;
 assert(flash_layout_tail_allocated()==0x10000);
 rows[2]=(esp_partition_t){0xa10000,0x500000};rows[3]=(esp_partition_t){0xf10000,0x6000};rows[4]=(esp_partition_t){0xf16000,0xda000};count=5;
 assert(flash_layout_tail_allocated()==0x5f0000);
 rows[3].size=0;assert(flash_layout_tail_allocated()==0x5ea000);
 count=1;rows[0]=(esp_partition_t){0xa00000,0x20000};assert(flash_layout_tail_allocated()==0x10000);
 rows[0]=(esp_partition_t){0xfff000,0x2000};assert(flash_layout_tail_allocated()==0);
 rows[0]=(esp_partition_t){0xfffff000,0x2000};assert(flash_layout_tail_allocated()==0);
 puts("Flash map: empty/clean/legacy layouts, actual allocated bytes, unallocated gaps and overflow bounds passed (ASan/UBSan).");
}
''')
 subprocess.run(['cc','-std=c99','-fsanitize=address,undefined','-I',str(p),'-I',str(r/'components/partitions_mgr/include'),str(p/'test.c'),'-o',str(p/'test')],env=env,check=True)
 subprocess.run([str(p/'test')],env=env,check=True)
