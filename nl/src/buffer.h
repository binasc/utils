#ifndef __BUFFER_H__
#define __BUFFER_H__

typedef struct nl_buf_s
{
    char    *buf;
    size_t  len;
} nl_buf_t;

typedef struct nl_packet_s
{
    nl_address_t        addr;
    nl_buf_t            buf;
} nl_packet_t;

#endif

