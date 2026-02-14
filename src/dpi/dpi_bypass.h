/*
 * dpi_bypass.h — Portable C library for DPI bypass packet processing
 *
 * Provides IP/TCP/UDP parsing, QUIC/TLS detection, and packet construction.
 * Used by Android (JNI) and iOS (Swift bridge) VPN packet processors.
 * No platform-specific dependencies.
 */

#ifndef DPI_BYPASS_H
#define DPI_BYPASS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Parsed packet info structures                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  version;       /* 4 for IPv4 */
    uint8_t  ihl;           /* header length in 32-bit words */
    uint8_t  protocol;      /* IPPROTO_TCP (6) or IPPROTO_UDP (17) */
    uint8_t  ttl;
    uint32_t src_addr;      /* network byte order */
    uint32_t dst_addr;      /* network byte order */
    int      header_len;    /* IP header length in bytes */
    int      total_len;     /* total IP packet length */
    const uint8_t *l4_data; /* pointer to L4 header (TCP/UDP) */
    int      l4_len;        /* length of L4 data (header + payload) */
} dpi_ip_info_t;

typedef struct {
    uint16_t src_port;      /* network byte order */
    uint16_t dst_port;      /* network byte order */
    int      header_len;    /* UDP header = 8 bytes */
    const uint8_t *payload;
    int      payload_len;
} dpi_udp_info_t;

typedef struct {
    uint16_t src_port;      /* network byte order */
    uint16_t dst_port;      /* network byte order */
    uint32_t seq;           /* network byte order */
    uint32_t ack;           /* network byte order */
    uint8_t  flags;         /* TCP flags (SYN=0x02, ACK=0x10, FIN=0x01, RST=0x04, PSH=0x08) */
    uint16_t window;        /* network byte order */
    int      header_len;    /* TCP header length in bytes (data offset * 4) */
    const uint8_t *payload;
    int      payload_len;
} dpi_tcp_info_t;

/* TCP flag constants */
#define DPI_TCP_FIN  0x01
#define DPI_TCP_SYN  0x02
#define DPI_TCP_RST  0x04
#define DPI_TCP_PSH  0x08
#define DPI_TCP_ACK  0x10

/* ------------------------------------------------------------------ */
/*  IP/TCP/UDP parsing                                                 */
/* ------------------------------------------------------------------ */

/*
 * Parse an IPv4 packet.
 * Returns 0 on success, -1 on error (too short, not IPv4, etc.)
 */
int dpi_parse_ipv4(const uint8_t *pkt, int len, dpi_ip_info_t *info);

/*
 * Parse a UDP header from L4 data.
 * l4 / l4_len come from dpi_ip_info_t.l4_data / l4_len.
 * Returns 0 on success, -1 on error.
 */
int dpi_parse_udp(const uint8_t *l4, int l4_len, dpi_udp_info_t *info);

/*
 * Parse a TCP header from L4 data.
 * Returns 0 on success, -1 on error.
 */
int dpi_parse_tcp(const uint8_t *l4, int l4_len, dpi_tcp_info_t *info);

/* ------------------------------------------------------------------ */
/*  Protocol detection                                                 */
/* ------------------------------------------------------------------ */

/*
 * Check if UDP payload is a QUIC Initial packet.
 * Checks for long header bit + QUIC v1 (0x00000001) or v2 (0x6b3343cf).
 */
bool dpi_is_quic_initial(const uint8_t *payload, int len);

/*
 * Check if TCP payload starts with a TLS ClientHello.
 * Checks: content_type=0x16 (handshake), handshake_type=0x01 (ClientHello).
 */
bool dpi_is_tls_client_hello(const uint8_t *payload, int len);

/* ------------------------------------------------------------------ */
/*  Packet construction (for writing back to TUN fd)                   */
/* ------------------------------------------------------------------ */

/*
 * Build a fake UDP packet (just UDP header + fake payload, no IP header).
 * Used on Android where raw sockets don't need IP_HDRINCL.
 * Returns total length written to out, or -1 on error.
 */
int dpi_build_fake_udp(uint8_t *out, int out_size,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *fake_payload, int fake_len);

/*
 * Build a full IPv4+UDP response packet for writing to TUN fd.
 * Constructs IP header + UDP header + payload.
 * Returns total length written to out, or -1 on error.
 */
int dpi_build_ipv4_udp(uint8_t *out, int out_size,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *payload, int payload_len);

/*
 * Build a full IPv4+TCP packet for writing to TUN fd.
 * Constructs IP header + TCP header + payload.
 * flags: DPI_TCP_SYN, DPI_TCP_ACK, etc.
 * Returns total length written to out, or -1 on error.
 */
int dpi_build_ipv4_tcp(uint8_t *out, int out_size,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint8_t flags, uint16_t window,
                       const uint8_t *payload, int payload_len);

/*
 * Compute the Internet checksum (RFC 1071).
 * Used for IP header checksum and TCP/UDP pseudo-header checksum.
 */
uint16_t dpi_checksum(const uint8_t *data, int len);

/*
 * Compute TCP or UDP checksum with pseudo-header.
 * proto: 6 for TCP, 17 for UDP.
 */
uint16_t dpi_transport_checksum(uint32_t src_addr, uint32_t dst_addr,
                                uint8_t proto,
                                const uint8_t *transport_hdr, int transport_len);

#ifdef __cplusplus
}
#endif

#endif /* DPI_BYPASS_H */
