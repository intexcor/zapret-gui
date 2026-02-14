/*
 * dpi_bypass.c — Portable C library for DPI bypass packet processing
 *
 * IP/TCP/UDP parsing, QUIC/TLS detection, packet construction.
 * No platform-specific headers — uses manual struct layouts.
 */

#include "dpi_bypass.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Wire format constants (avoid platform header dependencies)         */
/* ------------------------------------------------------------------ */

#define IPV4_MIN_HEADER   20
#define TCP_MIN_HEADER    20
#define UDP_HEADER_LEN     8

#define IPPROTO_TCP_CONST  6
#define IPPROTO_UDP_CONST 17

/* ------------------------------------------------------------------ */
/*  Byte-order helpers (portable, no htons/ntohs needed)               */
/* ------------------------------------------------------------------ */

static inline uint16_t read_u16_be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline void write_u16_be(uint8_t *p, uint16_t val)
{
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)(val & 0xFF);
}

static inline void write_u32_be(uint8_t *p, uint32_t val)
{
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)(val & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  Checksum (RFC 1071)                                                */
/* ------------------------------------------------------------------ */

uint16_t dpi_checksum(const uint8_t *data, int len)
{
    uint32_t sum = 0;
    int i;

    for (i = 0; i + 1 < len; i += 2)
        sum += read_u16_be(data + i);

    if (len & 1)
        sum += (uint32_t)data[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum & 0xFFFF);
}

uint16_t dpi_transport_checksum(uint32_t src_addr, uint32_t dst_addr,
                                uint8_t proto,
                                const uint8_t *transport_hdr, int transport_len)
{
    /* Pseudo-header: src_ip(4) + dst_ip(4) + zero(1) + proto(1) + length(2) = 12 bytes */
    uint8_t pseudo[12];
    write_u32_be(pseudo + 0, src_addr);
    write_u32_be(pseudo + 4, dst_addr);
    pseudo[8] = 0;
    pseudo[9] = proto;
    write_u16_be(pseudo + 10, (uint16_t)transport_len);

    uint32_t sum = 0;
    int i;

    /* Sum pseudo-header */
    for (i = 0; i < 12; i += 2)
        sum += read_u16_be(pseudo + i);

    /* Sum transport data */
    for (i = 0; i + 1 < transport_len; i += 2)
        sum += read_u16_be(transport_hdr + i);

    if (transport_len & 1)
        sum += (uint32_t)transport_hdr[transport_len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum & 0xFFFF);
}

/* ------------------------------------------------------------------ */
/*  IPv4 parsing                                                       */
/* ------------------------------------------------------------------ */

int dpi_parse_ipv4(const uint8_t *pkt, int len, dpi_ip_info_t *info)
{
    if (len < IPV4_MIN_HEADER)
        return -1;

    /* Version must be 4 */
    uint8_t ver_ihl = pkt[0];
    if ((ver_ihl >> 4) != 4)
        return -1;

    int ihl = (ver_ihl & 0x0F);
    int header_len = ihl * 4;
    if (header_len < IPV4_MIN_HEADER || header_len > len)
        return -1;

    int total_len = read_u16_be(pkt + 2);
    if (total_len > len)
        total_len = len; /* truncated packet — use what we have */

    info->version    = 4;
    info->ihl        = (uint8_t)ihl;
    info->ttl        = pkt[8];
    info->protocol   = pkt[9];
    info->src_addr   = read_u32_be(pkt + 12);
    info->dst_addr   = read_u32_be(pkt + 16);
    info->header_len = header_len;
    info->total_len  = total_len;
    info->l4_data    = pkt + header_len;
    info->l4_len     = total_len - header_len;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  UDP parsing                                                        */
/* ------------------------------------------------------------------ */

int dpi_parse_udp(const uint8_t *l4, int l4_len, dpi_udp_info_t *info)
{
    if (l4_len < UDP_HEADER_LEN)
        return -1;

    info->src_port    = read_u16_be(l4 + 0);
    info->dst_port    = read_u16_be(l4 + 2);
    info->header_len  = UDP_HEADER_LEN;
    info->payload     = l4 + UDP_HEADER_LEN;
    info->payload_len = l4_len - UDP_HEADER_LEN;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  TCP parsing                                                        */
/* ------------------------------------------------------------------ */

int dpi_parse_tcp(const uint8_t *l4, int l4_len, dpi_tcp_info_t *info)
{
    if (l4_len < TCP_MIN_HEADER)
        return -1;

    int data_offset = (l4[12] >> 4) * 4;
    if (data_offset < TCP_MIN_HEADER || data_offset > l4_len)
        return -1;

    info->src_port    = read_u16_be(l4 + 0);
    info->dst_port    = read_u16_be(l4 + 2);
    info->seq         = read_u32_be(l4 + 4);
    info->ack         = read_u32_be(l4 + 8);
    info->flags       = l4[13] & 0x3F;
    info->window      = read_u16_be(l4 + 14);
    info->header_len  = data_offset;
    info->payload     = l4 + data_offset;
    info->payload_len = l4_len - data_offset;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  QUIC Initial detection                                             */
/* ------------------------------------------------------------------ */

bool dpi_is_quic_initial(const uint8_t *payload, int len)
{
    if (len < 5)
        return false;

    /* Long header: bit 7 must be set */
    if ((payload[0] & 0x80) == 0)
        return false;

    /* Version field at bytes 1-4 */
    uint32_t version = read_u32_be(payload + 1);

    /* QUIC v1 or QUIC v2 */
    return version == 0x00000001 || version == 0x6b3343cf;
}

/* ------------------------------------------------------------------ */
/*  TLS ClientHello detection                                          */
/* ------------------------------------------------------------------ */

bool dpi_is_tls_client_hello(const uint8_t *payload, int len)
{
    if (len < 6)
        return false;

    /* ContentType: Handshake (0x16) */
    if (payload[0] != 0x16)
        return false;

    /* HandshakeType: ClientHello (0x01) at offset 5 */
    if (payload[5] != 0x01)
        return false;

    return true;
}

/* ------------------------------------------------------------------ */
/*  Build fake UDP packet (no IP header)                               */
/* ------------------------------------------------------------------ */

int dpi_build_fake_udp(uint8_t *out, int out_size,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *fake_payload, int fake_len)
{
    int total = UDP_HEADER_LEN + fake_len;
    if (out_size < total)
        return -1;

    /* UDP header */
    write_u16_be(out + 0, src_port);
    write_u16_be(out + 2, dst_port);
    write_u16_be(out + 4, (uint16_t)total);
    write_u16_be(out + 6, 0); /* checksum = 0 (optional for IPv4) */

    /* Payload */
    if (fake_len > 0)
        memcpy(out + UDP_HEADER_LEN, fake_payload, fake_len);

    return total;
}

/* ------------------------------------------------------------------ */
/*  Build IPv4 + UDP packet                                            */
/* ------------------------------------------------------------------ */

int dpi_build_ipv4_udp(uint8_t *out, int out_size,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *payload, int payload_len)
{
    int udp_len = UDP_HEADER_LEN + payload_len;
    int total   = IPV4_MIN_HEADER + udp_len;
    if (out_size < total)
        return -1;

    memset(out, 0, IPV4_MIN_HEADER);

    /* IPv4 header */
    out[0] = 0x45;                         /* version=4, IHL=5 */
    write_u16_be(out + 2, (uint16_t)total); /* total length */
    out[8] = 64;                            /* TTL */
    out[9] = IPPROTO_UDP_CONST;             /* protocol */
    write_u32_be(out + 12, src_addr);
    write_u32_be(out + 16, dst_addr);

    /* IP header checksum */
    uint16_t ip_cksum = dpi_checksum(out, IPV4_MIN_HEADER);
    write_u16_be(out + 10, ip_cksum);

    /* UDP header */
    uint8_t *udp = out + IPV4_MIN_HEADER;
    write_u16_be(udp + 0, src_port);
    write_u16_be(udp + 2, dst_port);
    write_u16_be(udp + 4, (uint16_t)udp_len);
    write_u16_be(udp + 6, 0); /* checksum placeholder */

    /* UDP payload */
    if (payload_len > 0)
        memcpy(udp + UDP_HEADER_LEN, payload, payload_len);

    /* UDP checksum */
    uint16_t udp_cksum = dpi_transport_checksum(src_addr, dst_addr,
                                                 IPPROTO_UDP_CONST,
                                                 udp, udp_len);
    if (udp_cksum == 0)
        udp_cksum = 0xFFFF; /* RFC 768: 0 means no checksum, use 0xFFFF */
    write_u16_be(udp + 6, udp_cksum);

    return total;
}

/* ------------------------------------------------------------------ */
/*  Build IPv4 + TCP packet                                            */
/* ------------------------------------------------------------------ */

int dpi_build_ipv4_tcp(uint8_t *out, int out_size,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint8_t flags, uint16_t window,
                       const uint8_t *payload, int payload_len)
{
    int tcp_len = TCP_MIN_HEADER + payload_len;
    int total   = IPV4_MIN_HEADER + tcp_len;
    if (out_size < total)
        return -1;

    memset(out, 0, IPV4_MIN_HEADER + TCP_MIN_HEADER);

    /* IPv4 header */
    out[0] = 0x45;                         /* version=4, IHL=5 */
    write_u16_be(out + 2, (uint16_t)total); /* total length */
    out[8] = 64;                            /* TTL */
    out[9] = IPPROTO_TCP_CONST;             /* protocol */
    write_u32_be(out + 12, src_addr);
    write_u32_be(out + 16, dst_addr);

    /* IP header checksum */
    uint16_t ip_cksum = dpi_checksum(out, IPV4_MIN_HEADER);
    write_u16_be(out + 10, ip_cksum);

    /* TCP header */
    uint8_t *tcp = out + IPV4_MIN_HEADER;
    write_u16_be(tcp + 0, src_port);
    write_u16_be(tcp + 2, dst_port);
    write_u32_be(tcp + 4, seq);
    write_u32_be(tcp + 8, ack);
    tcp[12] = (TCP_MIN_HEADER / 4) << 4;   /* data offset */
    tcp[13] = flags;
    write_u16_be(tcp + 14, window);
    /* checksum at tcp+16, urgent at tcp+18 — both 0 initially */

    /* TCP payload */
    if (payload_len > 0)
        memcpy(tcp + TCP_MIN_HEADER, payload, payload_len);

    /* TCP checksum */
    uint16_t tcp_cksum = dpi_transport_checksum(src_addr, dst_addr,
                                                 IPPROTO_TCP_CONST,
                                                 tcp, tcp_len);
    write_u16_be(tcp + 16, tcp_cksum);

    return total;
}
