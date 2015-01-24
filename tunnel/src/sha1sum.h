#ifndef __SHA1SUM_H__
#define __SHA1SUM_H__

#undef BIG_ENDIAN_HOST
typedef unsigned int u32;

typedef struct {
    u32  h0,h1,h2,h3,h4;
    u32  nblocks;
    unsigned char buf[64];
    int  count;
} SHA1_CONTEXT;

void sha1_init(SHA1_CONTEXT *hd);
void sha1_write(SHA1_CONTEXT *hd, unsigned char *inbuf, size_t inlen);
void sha1_final(SHA1_CONTEXT *hd);
#endif

