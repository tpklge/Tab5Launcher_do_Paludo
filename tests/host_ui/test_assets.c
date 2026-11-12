#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_assets.h"
#include "../../components/ui/ui_assets.c"
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) { lv_display_flush_ready(d); }
int main(int argc,char **argv)
{
 assert(argc==3); lv_init(); lv_display_t *d=lv_display_create(1280,720); static uint8_t pixels[1280*20*4]; lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
 ui_assets_init();
 for(int i=0;i<100;i++) {
  lv_obj_t *o=lv_image_create(lv_screen_active()); assert(ui_assets_set(o,"/sdcard/icon.png",256,256,64,64));
  request_t job; assert(xQueueReceive(s_jobs,&job,0)); snprintf(job.path,sizeof(job.path),"%s",argv[i%2+1]); job.max_w=1280; job.max_h=720;
  result_t r=decode_impl(&job); assert(r.pixels && r.w && r.h);
  if(i%2) blur(&r);
  xQueueSend(s_results,&r,0); poll(NULL); assert(lv_image_get_src(o)!=NULL); lv_obj_delete(o);
 }
 request_t job={.id=123,.max_w=1,.max_h=1}; snprintf(job.path,sizeof(job.path),"%s",argv[1]); result_t r=decode_impl(&job); assert(!r.pixels);
 lv_obj_t *old=lv_image_create(lv_screen_active()); assert(ui_assets_set(old,"/sdcard/a.png",256,256,64,64)); xQueueReceive(s_jobs,&job,0); lv_obj_delete(old);
 lv_obj_t *fresh=lv_image_create(lv_screen_active()); assert(ui_assets_set(fresh,"/sdcard/b.png",256,256,64,64));
 snprintf(job.path,sizeof(job.path),"%s",argv[1]); job.max_w=1280; job.max_h=720; r=decode_impl(&job); assert(r.pixels); xQueueSend(s_results,&r,0); poll(NULL); assert(!lv_image_get_src(fresh));
 lv_obj_delete(fresh); free(s_jobs); free(s_results); lv_deinit(); puts("Assets: real PNG/JPEG decoding, size rejection, blur, stale replies and 100 lifecycles passed (ASan/UBSan).");
}
