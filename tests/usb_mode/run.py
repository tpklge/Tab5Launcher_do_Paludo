"""Production USB mode controller isolated from the GUI/USB peripheral.
Uses the real storage coordinator; task, policy and RTC marker failures are injected.
"""
from pathlib import Path
import os,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'components/usb_disk/usb_disk.c').read_text();a=s.index('#define USB_REQUEST_MAGIC');b=s.index('static bool msc_active;',a)
prefix=r'''
#include "usb_disk.h"
#include "usb_storage.h"
#include "storage_guard.h"
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define RTC_NOINIT_ATTR
#define ESP_RST_SW 3
static int reset_reason=ESP_RST_SW;
static int esp_reset_reason(void){return reset_reason;}
#define ESP_LOGE(...) ((void)0)
static bool allowed=true,task_fail,drive_busy,restarted;
static jmp_buf stopped;
static void (*task_fn)(void*);
static bool check(void){return allowed;}
static int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned p,void*h){if(task_fail)return 0;task_fn=fn;return 1;}
static void vTaskDelay(unsigned ticks){}
static void vTaskDelete(void*p){longjmp(stopped,1);}
void usb_disk_restart(void){restarted=true;assert(storage_exclusive_owned());longjmp(stopped,1);}
esp_err_t usb_storage_eject(void){return 0;}
void usb_storage_status(usb_storage_status_t*out){*out=(usb_storage_status_t){.mounted=drive_busy};}
'''
main=r'''
static void run_case(unsigned scenario){
 usb_disk_set_transition_check(check);
 if(scenario==0){task_fail=true;assert(usb_disk_request()==ESP_ERR_NO_MEM);assert(mode_request.magic==0&&!storage_io_busy());return;}
 if(scenario==1){allowed=false;assert(usb_disk_request()==ESP_ERR_INVALID_STATE);assert(mode_request.magic==0&&!task_fn);return;}
 if(scenario==2){assert(storage_write_acquire());assert(usb_disk_request()==ESP_ERR_INVALID_STATE);assert(mode_request.magic==0);storage_write_release();return;}
 assert(usb_disk_request()==ESP_OK);assert(task_fn&&mode_request.magic==0);
 if(scenario==3)allowed=false; /* State changed after accepting request. */
 if(scenario==4)allowed=false;
 if(scenario==5)drive_busy=true; /* Ejection never completed. */
 if(scenario==6)assert(storage_io_acquire()); /* I/O started before worker. */
 if(!setjmp(stopped))task_fn(NULL);
 if(scenario==7){assert(restarted&&mode_request.magic==USB_REQUEST_MAGIC);assert(storage_exclusive_owned());
 assert(usb_disk_boot_requested());assert(!usb_disk_boot_requested());
 persist_request();reset_reason=0;assert(!usb_disk_boot_requested());reset_reason=ESP_RST_SW;
 persist_request();mode_request.inverse=0;assert(!usb_disk_boot_requested());}
 else {assert(!restarted&&mode_request.magic==0&&usb_disk_transition_error()!=ESP_OK);if(scenario==6)storage_io_release();assert(!storage_io_busy());}
}
int main(void){for(unsigned i=0;i<8;i++){pid_t pid=fork();assert(pid>=0);if(!pid){run_case(i);_exit(0);}int status;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);}puts("USB mode: no boot flag before drain, failed task/policy/ejection, RTC single consumption/reset/corruption, late I/O, exclusive success and failure cleanup passed.");}
'''
env=os.environ.copy();sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
if sdk.exists():env.setdefault('SDKROOT',str(sdk))
with tempfile.TemporaryDirectory() as t:
 c=Path(t)/'controller.c';c.write_text(prefix+s[a:b]+main);exe=Path(t)/'test'
 cmd=['cc','-std=gnu11','-g','-fsanitize=address,undefined','-I',str(r/'tests/installer/stubs')]
 for comp in ('storage','usb_disk','usb_storage'):cmd+=['-I',str(r/'components'/comp/'include')]
 cmd+=[str(c),str(r/'components/storage/storage_guard.c'),'-o',str(exe)]
 subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe)],check=True,env=env)
