/*
 * tcp_relay.c — Android TCP relay with TLS ClientHello split
 */

#include "tcp_relay.h"
#include "dpi_bypass.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <android/log.h>

#define TAG "tcp-relay"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define MAX_PKT_SIZE 65536
#define TUN_ADDR     0x0A780001  /* 10.120.0.1 */

static int64_t monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec;
}

static tcp_session_t *find_session(tcp_relay_t *relay,
                                   uint16_t src_port, uint32_t dst_addr, uint16_t dst_port)
{
    for (int i = 0; i < relay->session_count; i++) {
        tcp_session_t *s = &relay->sessions[i];
        if (s->active &&
            s->src_port == src_port &&
            s->dst_addr == dst_addr &&
            s->dst_port == dst_port) {
            return s;
        }
    }
    return NULL;
}

static tcp_session_t *find_session_by_fd(tcp_relay_t *relay, int fd)
{
    for (int i = 0; i < relay->session_count; i++) {
        if (relay->sessions[i].active && relay->sessions[i].fd == fd)
            return &relay->sessions[i];
    }
    return NULL;
}

/* Send a TCP packet to the TUN (towards the app) */
static void send_to_tun(tcp_relay_t *relay, tcp_session_t *session,
                         uint8_t flags, const uint8_t *payload, int payload_len)
{
    uint8_t pkt[MAX_PKT_SIZE];
    int pkt_len = dpi_build_ipv4_tcp(pkt, sizeof(pkt),
                                      session->dst_addr,
                                      relay->tun_addr,
                                      session->dst_port,
                                      session->src_port,
                                      session->tun_seq,
                                      session->tun_ack,
                                      flags,
                                      32768,  /* window */
                                      payload, payload_len);
    if (pkt_len > 0)
        write(relay->tun_fd, pkt, pkt_len);

    /* Advance our seq for data/SYN/FIN (they consume sequence space) */
    if (payload_len > 0)
        session->tun_seq += payload_len;
    if (flags & (DPI_TCP_SYN | DPI_TCP_FIN))
        session->tun_seq += 1;
}

/* Create a non-blocking protected TCP socket and initiate connect */
static int create_protected_socket(tcp_relay_t *relay,
                                   uint32_t dst_addr, uint16_t dst_port)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        LOGE("socket(SOCK_STREAM): %s", strerror(errno));
        return -1;
    }

    /* Protect from VPN routing */
    jboolean ok = (*relay->env)->CallBooleanMethod(relay->env,
                                                    relay->vpn_service,
                                                    relay->protect_method,
                                                    fd);
    if (!ok) {
        LOGE("VpnService.protect() failed for tcp fd=%d", fd);
        close(fd);
        return -1;
    }

    /* Disable Nagle for low-latency relay */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* Initiate non-blocking connect */
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(dst_port);
    dst.sin_addr.s_addr = htonl(dst_addr);

    int ret = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
    if (ret < 0 && errno != EINPROGRESS) {
        LOGE("connect(tcp): %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void close_session(tcp_session_t *session)
{
    if (session->fd >= 0)
        close(session->fd);
    session->fd = -1;
    session->state = TCP_STATE_CLOSED;
    session->active = false;
}

static void handle_syn(tcp_relay_t *relay,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq)
{
    /* Find existing or allocate new session */
    tcp_session_t *session = find_session(relay, src_port, dst_addr, dst_port);
    if (session) {
        /* Re-SYN: close old connection and start fresh */
        close_session(session);
    }

    /* Find free slot */
    tcp_session_t *slot = NULL;
    for (int i = 0; i < relay->session_count; i++) {
        if (!relay->sessions[i].active) {
            slot = &relay->sessions[i];
            break;
        }
    }
    if (!slot) {
        if (relay->session_count >= TCP_MAX_SESSIONS) {
            LOGE("TCP session limit reached");
            return;
        }
        slot = &relay->sessions[relay->session_count++];
    }

    int fd = create_protected_socket(relay, dst_addr, dst_port);
    if (fd < 0)
        return;

    memset(slot, 0, sizeof(*slot));
    slot->src_port       = src_port;
    slot->dst_addr       = dst_addr;
    slot->dst_port       = dst_port;
    slot->fd             = fd;
    slot->state          = TCP_STATE_SYN_RECEIVED;
    slot->active         = true;
    slot->first_data_sent = false;
    slot->last_activity  = monotonic_seconds();
    slot->app_isn        = seq;

    /* Our ISN: use a simple counter derived from time */
    slot->tun_seq = (uint32_t)(monotonic_seconds() * 1000) ^ (dst_port << 16 | src_port);
    slot->tun_ack = seq + 1;  /* ACK the SYN */

    /* Send SYN-ACK back to the app via TUN */
    send_to_tun(relay, slot, DPI_TCP_SYN | DPI_TCP_ACK, NULL, 0);

    slot->state = TCP_STATE_ESTABLISHED;
}

static void handle_data(tcp_relay_t *relay, tcp_session_t *session,
                        const uint8_t *payload, int payload_len, uint32_t seq)
{
    if (session->state != TCP_STATE_ESTABLISHED)
        return;

    session->last_activity = monotonic_seconds();
    session->tun_ack = seq + payload_len;

    /* Check if this is the first data segment and contains a TLS ClientHello */
    if (!session->first_data_sent && relay->split_pos > 0 &&
        payload_len > relay->split_pos &&
        dpi_is_tls_client_hello(payload, payload_len)) {

        LOGD("TLS ClientHello detected, splitting at pos %d", relay->split_pos);

        int pos = relay->split_pos;
        if (relay->use_disorder) {
            /* Send second part first (disorder) */
            send(session->fd, payload + pos, payload_len - pos, 0);
            send(session->fd, payload, pos, 0);
        } else {
            /* Normal split: first part, then second */
            send(session->fd, payload, pos, 0);
            send(session->fd, payload + pos, payload_len - pos, 0);
        }
        session->first_data_sent = true;
    } else {
        /* Forward as-is */
        send(session->fd, payload, payload_len, 0);
        if (payload_len > 0)
            session->first_data_sent = true;
    }

    /* ACK the data back to the app */
    send_to_tun(relay, session, DPI_TCP_ACK, NULL, 0);
}

static void handle_fin(tcp_relay_t *relay, tcp_session_t *session, uint32_t seq)
{
    session->tun_ack = seq + 1;

    /* ACK the FIN */
    send_to_tun(relay, session, DPI_TCP_ACK, NULL, 0);

    /* Shutdown our side of the real connection */
    if (session->fd >= 0)
        shutdown(session->fd, SHUT_WR);

    session->state = TCP_STATE_FIN_WAIT;
}

static void handle_rst(tcp_relay_t *relay, tcp_session_t *session)
{
    (void)relay;
    close_session(session);
}

void tcp_relay_init(tcp_relay_t *relay, int tun_fd,
                    int split_pos, bool use_disorder,
                    JNIEnv *env, jobject vpn_service)
{
    memset(relay, 0, sizeof(*relay));
    relay->tun_fd       = tun_fd;
    relay->tun_addr     = TUN_ADDR;
    relay->split_pos    = split_pos;
    relay->use_disorder = use_disorder;
    relay->env          = env;
    relay->vpn_service  = vpn_service;

    jclass cls = (*env)->GetObjectClass(env, vpn_service);
    relay->protect_method = (*env)->GetMethodID(env, cls, "protect", "(I)Z");
}

void tcp_relay_process(tcp_relay_t *relay,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       uint32_t seq, uint32_t ack,
                       uint8_t flags,
                       const uint8_t *payload, int payload_len)
{
    (void)src_addr;
    (void)ack;

    if (flags & DPI_TCP_RST) {
        tcp_session_t *session = find_session(relay, src_port, dst_addr, dst_port);
        if (session)
            handle_rst(relay, session);
        return;
    }

    if (flags & DPI_TCP_SYN) {
        handle_syn(relay, src_addr, dst_addr, src_port, dst_port, seq);
        return;
    }

    tcp_session_t *session = find_session(relay, src_port, dst_addr, dst_port);
    if (!session)
        return;

    if (flags & DPI_TCP_FIN) {
        handle_fin(relay, session, seq);
        return;
    }

    if (payload_len > 0) {
        handle_data(relay, session, payload, payload_len, seq);
    }
}

int tcp_relay_handle_response(tcp_relay_t *relay, int fd)
{
    tcp_session_t *session = find_session_by_fd(relay, fd);
    if (!session)
        return 0;

    uint8_t buf[MAX_PKT_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n > 0) {
        session->last_activity = monotonic_seconds();
        /* Send data to app via TUN */
        send_to_tun(relay, session, DPI_TCP_ACK | DPI_TCP_PSH, buf, (int)n);
        return 1;
    }

    if (n == 0) {
        /* Server closed connection — send FIN to app */
        send_to_tun(relay, session, DPI_TCP_FIN | DPI_TCP_ACK, NULL, 0);
        close_session(session);
        return 1;
    }

    /* n < 0 */
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 1;

    /* Error — send RST to app */
    send_to_tun(relay, session, DPI_TCP_RST, NULL, 0);
    close_session(session);
    return -1;
}

int tcp_relay_get_fds(tcp_relay_t *relay, int *out_fds, int max_fds)
{
    int count = 0;
    for (int i = 0; i < relay->session_count && count < max_fds; i++) {
        if (relay->sessions[i].active && relay->sessions[i].fd >= 0)
            out_fds[count++] = relay->sessions[i].fd;
    }
    return count;
}

void tcp_relay_cleanup(tcp_relay_t *relay)
{
    int64_t now = monotonic_seconds();
    for (int i = 0; i < relay->session_count; i++) {
        tcp_session_t *s = &relay->sessions[i];
        if (s->active && (now - s->last_activity) > TCP_SESSION_TIMEOUT) {
            /* Send RST to app before closing */
            send_to_tun(relay, s, DPI_TCP_RST, NULL, 0);
            close_session(s);
        }
    }
}

void tcp_relay_destroy(tcp_relay_t *relay)
{
    for (int i = 0; i < relay->session_count; i++) {
        if (relay->sessions[i].active)
            close_session(&relay->sessions[i]);
    }
    relay->session_count = 0;
}
