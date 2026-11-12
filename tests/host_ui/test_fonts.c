#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static lv_font_t *load_memory(const char *path){FILE *f=fopen(path+2,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);void *raw=malloc(size);assert(raw&&fread(raw,1,size,f)==(size_t)size);fclose(f);lv_font_t *font=lv_binfont_create_from_buffer(raw,size);free(raw);return font;}
int main(int argc,char **argv){assert(argc==3);lv_init();lv_font_t *cjk=load_memory(argv[1]),*emoji=load_memory(argv[2]);assert(cjk&&emoji);cjk->fallback=emoji;lv_font_glyph_dsc_t glyph;const uint32_t letters[]={0x4e2d,0x6587,0x3042,0x30a2,0x1f600};for(unsigned i=0;i<5;i++){assert(lv_font_get_glyph_dsc(cjk,&glyph,letters[i],0));assert(!glyph.is_placeholder&&glyph.box_w&&glyph.box_h);}cjk->fallback=NULL;lv_binfont_destroy(cjk);lv_binfont_destroy(emoji);puts("SD fonts: memory loader, released source buffer and Han/Hiragana/Katakana/emoji fallback passed (ASan/UBSan).");}
