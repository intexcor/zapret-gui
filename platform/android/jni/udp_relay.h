/*
 * udp_relay.h — Android UDP relay with QUIC fake injection
 *
 * Manages UDP sessions: (src_port, dst_ip, dst_port) → protected socket.
 * Detects QUIC Initial packets and injects fake packets with low TTL.
 */

#ifndef UDP_RELAY_H
#define UDP_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include <jni.h>

#define UDP_MAX_SESSIONS     4096
#define UDP_SESSION_TIMEOUT  120  /* seconds */

typedef struct {
    uint16_t src_port;   /* app-side source port (network byte order) */
    uint32_t dst_addr;   /* destination IP (network byte order) */
    uint16_t dst_port;   /* destination port (network byte order) */
    int      fd;         /* protected UDP socket */
    int64_t  last_activity; /* monotonic timestamp (seconds) */
    bool     active;
} udp_session_t;

typedef struct {
    udp_session_t sessions[UDP_MAX_SESSIONS];
    int session_count;

    /* Fake injection config */
    const uint8_t *fake_payload;
    int fake_len;
    int fake_ttl;
    int fake_repeats;

    /* TUN fd for sending responses back to app */
    int tun_fd;

    /* JNI references for socket protection */
    JNIEnv *env;
    jobject vpn_service;
    jmethodID protect_method;
} udp_relay_t;

/*
 * Initialize the UDP relay.
 */
void udp_relay_init(udp_relay_t *relay, int tun_fd,
                    const uint8_t *fake_payload, int fake_len,
                    int fake_ttl, int fake_repeats,
                    JNIEnv *env, jobject vpn_service);

/*
 * Process an outgoing UDP packet from the TUN (app → internet).
 * Creates/reuses session, detects QUIC, injects fakes, forwards.
 *
 * src_addr/dst_addr in network byte order (as parsed by dpi_parse_ipv4).
 */
void udp_relay_process(udp_relay_t *relay,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *payload, int payload_len);

/*
 * Check a relay socket fd for incoming response data.
 * Constructs IP+UDP response and writes to TUN.
 * Returns: 1 if data was processed, 0 if fd doesn't belong to relay, -1 on error.
 */
int udp_relay_handle_response(udp_relay_t *relay, int fd);

/*
 * Get all active session fds for epoll registration.
 * Returns number of fds written to out_fds (up to max_fds).
 */
int udp_relay_get_fds(udp_relay_t *relay, int *out_fds, int max_fds);

/*
 * Clean up expired sessions (older than UDP_SESSION_TIMEOUT).
 */
void udp_relay_cleanup(udp_relay_t *relay);

/*
 * Destroy all sessions and free resources.
 */
void udp_relay_destroy(udp_relay_t *relay);

#endif /* UDP_RELAY_H */
