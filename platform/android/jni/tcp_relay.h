/*
 * tcp_relay.h — Android TCP relay with TLS ClientHello split
 *
 * Manages TCP sessions: TUN app ↔ protected real socket.
 * Implements a lightweight TCP state machine for the TUN side,
 * while using connected sockets for the real-internet side.
 *
 * For TLS ClientHello, splits the first data segment at split_pos
 * to bypass DPI inspection.
 */

#ifndef TCP_RELAY_H
#define TCP_RELAY_H

#include <stdint.h>
#include <stdbool.h>
#include <jni.h>

#define TCP_MAX_SESSIONS   2048
#define TCP_SESSION_TIMEOUT 300  /* seconds */

typedef enum {
    TCP_STATE_IDLE = 0,
    TCP_STATE_SYN_RECEIVED,    /* Got SYN from app, connecting to dst */
    TCP_STATE_ESTABLISHED,     /* Connected, relaying data */
    TCP_STATE_FIN_WAIT,        /* App sent FIN, waiting for dst close */
    TCP_STATE_CLOSED
} tcp_state_t;

typedef struct {
    /* Session key */
    uint16_t src_port;    /* app-side source port */
    uint32_t dst_addr;    /* destination IP */
    uint16_t dst_port;    /* destination port */

    /* State */
    tcp_state_t state;
    int fd;               /* protected TCP socket to real server */
    bool active;
    bool first_data_sent; /* have we sent the first data segment? (for split) */
    int64_t last_activity;

    /* Sequence/ack tracking for TUN side */
    uint32_t tun_seq;     /* our seq number (server→app direction) */
    uint32_t tun_ack;     /* our ack number (what we've received from app) */
    uint32_t app_isn;     /* app's initial sequence number from SYN */
} tcp_session_t;

typedef struct {
    tcp_session_t sessions[TCP_MAX_SESSIONS];
    int session_count;

    /* DPI bypass config */
    int split_pos;        /* position to split TLS ClientHello (0 = no split) */
    bool use_disorder;    /* send second segment first (disorder mode) */

    /* TUN fd for sending responses back to app */
    int tun_fd;

    /* Source address for TUN responses (10.120.0.1) */
    uint32_t tun_addr;

    /* JNI references for socket protection */
    JNIEnv *env;
    jobject vpn_service;
    jmethodID protect_method;
} tcp_relay_t;

/*
 * Initialize the TCP relay.
 */
void tcp_relay_init(tcp_relay_t *relay, int tun_fd,
                    int split_pos, bool use_disorder,
                    JNIEnv *env, jobject vpn_service);

/*
 * Process an outgoing TCP packet from the TUN (app → internet).
 * Handles SYN, data, FIN, RST.
 */
void tcp_relay_process(tcp_relay_t *relay,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint8_t flags,
                       const uint8_t *payload, int payload_len);

/*
 * Check a relay socket fd for incoming response data.
 * Reads from server, constructs IP+TCP response, writes to TUN.
 * Returns: 1 if data was processed, 0 if fd doesn't belong to relay, -1 on error.
 */
int tcp_relay_handle_response(tcp_relay_t *relay, int fd);

/*
 * Get all active session fds for epoll registration.
 */
int tcp_relay_get_fds(tcp_relay_t *relay, int *out_fds, int max_fds);

/*
 * Clean up expired sessions.
 */
void tcp_relay_cleanup(tcp_relay_t *relay);

/*
 * Destroy all sessions.
 */
void tcp_relay_destroy(tcp_relay_t *relay);

#endif /* TCP_RELAY_H */
