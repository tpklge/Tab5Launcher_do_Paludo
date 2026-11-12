#define TEST_SLOT_SIZE (8*1024*1024)
#define main small_regression_main
#include "test.c"
#undef main
int main(int argc,char **argv)
{
 assert(argc==2); memset(slot,255,sizeof(slot)); assert(installer_init()==ESP_OK);
 char path[512],error[512]; snprintf(path,sizeof(path),SD_FILES_ROOT "/large.bin"); copy_file(argv[1],path);
 install_plan_t plan; assert(installer_plan(path,&plan,error,sizeof(error))==ESP_OK);
 assert(plan.firmware.image.image_size>4*1024*1024 && plan.firmware.image.image_size<sizeof(slot));
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))==ESP_OK);
 assert(stored==plan.firmware.image.image_size && phase==2);
 uint8_t actual[32]; SHA256(slot,stored,actual); assert(!memcmp(actual,plan.firmware.sha256,32));
 puts("Large app: synthetic 5 MB image streamed in <=4096-byte writes to an 8 MB slot and hash/SDK pipeline verified (ASan/UBSan; not physical compatibility).");
}
