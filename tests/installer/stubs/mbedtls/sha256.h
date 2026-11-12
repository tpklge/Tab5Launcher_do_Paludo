#pragma once
#include <openssl/sha.h>
#include <string.h>
typedef SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(SHA256_CTX *ctx) { memset(ctx,0,sizeof(*ctx)); }
static inline int mbedtls_sha256_starts(SHA256_CTX *ctx,int short_hash) { return SHA256_Init(ctx)?0:-1; }
static inline int mbedtls_sha256_update(SHA256_CTX *ctx,const unsigned char *data,size_t size) { return SHA256_Update(ctx,data,size)?0:-1; }
static inline int mbedtls_sha256_finish(SHA256_CTX *ctx,unsigned char out[32]) { return SHA256_Final(out,ctx)?0:-1; }
static inline void mbedtls_sha256_free(SHA256_CTX *ctx) { memset(ctx,0,sizeof(*ctx)); }

static inline int mbedtls_sha256(const unsigned char *b,size_t n,unsigned char out[32],int short_hash) { return SHA256(b,n,out)?0:-1; }
