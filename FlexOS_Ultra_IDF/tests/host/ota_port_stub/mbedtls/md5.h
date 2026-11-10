#pragma once
#include <stddef.h>
// MD5 real (implementacion propia en test_ota_port.c): lo que se compara con el manifiesto tiene que ser el MD5 de verdad
typedef struct {
    unsigned state[4];
    unsigned long long count;
    unsigned char buf[64];
} mbedtls_md5_context;
void mbedtls_md5_init(mbedtls_md5_context *c);
int mbedtls_md5_starts(mbedtls_md5_context *c);
int mbedtls_md5_update(mbedtls_md5_context *c, const unsigned char *in, size_t n);
int mbedtls_md5_finish(mbedtls_md5_context *c, unsigned char out[16]);
void mbedtls_md5_free(mbedtls_md5_context *c);
