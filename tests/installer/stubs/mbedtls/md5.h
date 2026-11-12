#pragma once
#include <openssl/md5.h>
static inline int mbedtls_md5(const unsigned char *b,size_t n,unsigned char out[16]) { return MD5(b,n,out)?0:-1; }
