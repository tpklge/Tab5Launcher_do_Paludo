#pragma once
#include <stdbool.h>
/* Preflight before cJSON's recursive parser. Does not replace syntax/schema
 * validation. Firmware workers have finite stacks, even for untrusted SD JSON. */
static inline bool json_depth_valid(const char *text,unsigned maximum) {
 if(!text || !maximum)return false;
 unsigned depth=0;bool string=false,escape=false;
 for(const unsigned char *p=(const unsigned char*)text;*p;p++) {
  if(string){if(escape)escape=false;else if(*p=='\\')escape=true;else if(*p=='"')string=false;continue;}
  if(*p=='"')string=true;
  else if(*p=='{' || *p=='['){if(++depth>maximum)return false;}
  else if(*p=='}' || *p==']'){if(!depth)return false;--depth;}
 }
 return !string && !depth;
}
