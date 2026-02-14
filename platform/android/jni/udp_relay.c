/*
 * udp_relay.c — Android UDP relay with QUIC fake injection
 */

#include "udp_relay.h"
#include "dpi_bypass.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <android/log.h>

#define TAG "udp-relay"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define MAX_PKT_SIZE 65536

static int64_t monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec;
}

/* Find existing session or return NULL */
static udp_session_t *find_session(udp_relay_t *relay,
                                   uint16_t src_port, uint32_t dst_addr, uint16_t dst_port)
{
    for (int i = 0; i < relay->session_count; i++) {
        udp_session_t *s = &relay->sessions[i];
        if (s->active &&
            s->src_port == src_port &&
            s->dst_addr == dst_addr &&
            s->dst_port == dst_port) {
            return s;
        }
    }
    return NULL;
}

/* Find session by its socket fd */
static udp_session_t *find_session_by_fd(udp_relay_t *relay, int fd)
{
    for (int i = 0; i < relay->session_count; i++) {
        if (relay->sessions[i].active && relay->sessions[i].fd == fd)
            return &relay->sessions[i];
    }
    return NULL;
}

/* Create a protected UDP socket and connect it to dst */
static int create_protected_socket(udp_relay_t *relay,
                                   uint32_t dst_addr, uint16_t dst_port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGE("socket(SOCK_DGRAM): %s", strerror(errno));
        return -1;
    }

    /* Protect socket from VPN routing (bypass the tunnel) */
    jboolean ok = (*relay->env)->CallBooleanMethod(relay->env,
                                                    relay->vpn_service,
                                                    relay->protect_method,
                                                    fd);
    if (!ok) {
        LOGE("VpnService.protect() failed for fd=%d", fd);
        close(fd);
        return -1;
    }

    /* Connect to destination so recv() returns only packets from this peer */
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(dst_port);
    dst.sin_addr.s_addr = htonl(dst_addr);

    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        LOGE("connect(udp): %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* Create or get existing session */
static udp_session_t *get_or_create_session(udp_relay_t *relay,
                                            uint16_t src_port,
                                            uint32_t dst_addr, uint16_t dst_port)
{
    udp_session_t *s = find_session(relay, src_port, dst_addr, dst_port);
    if (s) {
        s->last_activity = monotonic_seconds();
        return s;
    }

    /* Find a free slot (reuse inactive) */
    udp_session_t *slot = NULL;
    for (int i = 0; i < relay->session_count; i++) {
        if (!relay->sessions[i].active) {
            slot = &relay->sessions[i];
            break;
        }
    }
    if (!slot) {
        if (relay->session_count >= UDP_MAX_SESSIONS) {
            LOGE("UDP session limit reached (%d)", UDP_MAX_SESSIONS);
            return NULL;
        }
        slot = &relay->sessions[relay->session_count++];
    }

    int fd = create_protected_socket(relay, dst_addr, dst_port);
    if (fd < 0)
        return NULL;

    slot->src_port      = src_port;
    slot->dst_addr      = dst_addr;
    slot->dst_port      = dst_port;
    slot->fd            = fd;
    slot->last_activity = monotonic_seconds();
    slot->active        = true;

    return slot;
}

/* Send fake QUIC packets with low TTL, then the original */
static void send_with_fakes(udp_relay_t *relay, udp_session_t *session,
                            const uint8_t *payload, int payload_len)
{
    /* Set low TTL for fakes */
    int ttl = relay->fake_ttl;
    setsockopt(session->fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

    /* Send N fake packets */
    for (int i = 0; i < relay->fake_repeats; i++) {
        send(session->fd, relay->fake_payload, relay->fake_len, 0);
    }

    /* Restore normal TTL and send original */
    ttl = 64;
    setsockopt(session->fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    send(session->fd, payload, payload_len, 0);
}

void udp_relay_init(udp_relay_t *relay, int tun_fd,
                    const uint8_t *fake_payload, int fake_len,
                    int fake_ttl, int fake_repeats,
                    JNIEnv *env, jobject vpn_service)
{
    memset(relay, 0, sizeof(*relay));
    relay->tun_fd       = tun_fd;
    relay->fake_payload = fake_payload;
    relay->fake_len     = fake_len;
    relay->fake_ttl     = fake_ttl;
    relay->fake_repeats = fake_repeats;
    relay->env          = env;
    relay->vpn_service  = vpn_service;

    jclass cls = (*env)->GetObjectClass(env, vpn_service);
    relay->protect_method = (*env)->GetMethodID(env, cls, "protect", "(I)Z");
}

void udp_relay_process(udp_relay_t *relay,
                       uint32_t src_addr, uint32_t dst_addr,
                       uint16_t src_port, uint16_t dst_port,
                       const uint8_t *payload, int payload_len)
{
    udp_session_t *session = get_or_create_session(relay, src_port, dst_addr, dst_port);
    if (!session)
        return;

    /* Check if this is a QUIC Initial and we have fake payload */
    if (relay->fake_payload && relay->fake_len > 0 &&
        dpi_is_quic_initial(payload, payload_len)) {
        LOGD("QUIC Initial detected, injecting %d fakes (TTL=%d)",
             relay->fake_repeats, relay->fake_ttl);
        send_with_fakes(relay, session, payload, payload_len);
    } else {
        /* Forward as-is */
        send(session->fd, payload, payload_len, 0);
    }
}

int udp_relay_handle_response(udp_relay_t *relay, int fd)
{
    udp_session_t *session = find_session_by_fd(relay, fd);
    if (!session)
        return 0;

    uint8_t recv_buf[MAX_PKT_SIZE];
    ssize_t n = recv(fd, recv_buf, sizeof(recv_buf), 0);
    if (n <= 0)
        return -1;

    session->last_activity = monotonic_seconds();

    /* Build IP+UDP response packet for TUN */
    uint8_t pkt[MAX_PKT_SIZE];
    int pkt_len = dpi_build_ipv4_udp(pkt, sizeof(pkt),
                                      session->dst_addr,  /* response: dst→src */
                                      0x0A780001,          /* 10.120.0.1 (TUN addr) */
                                      session->dst_port,
                                      session->src_port,
                                      recv_buf, (int)n);
    if (pkt_len < 0)
        return -1;

    write(relay->tun_fd, pkt, pkt_len);
    return 1;
}

int udp_relay_get_fds(udp_relay_t *relay, int *out_fds, int max_fds)
{
    int count = 0;
    for (int i = 0; i < relay->session_count && count < max_fds; i++) {
        if (relay->sessions[i].active)
            out_fds[count++] = relay->sessions[i].fd;
    }
    return count;
}

void udp_relay_cleanup(udp_relay_t *relay)
{
    int64_t now = monotonic_seconds();
    for (int i = 0; i < relay->session_count; i++) {
        udp_session_t *s = &relay->sessions[i];
        if (s->active && (now - s->last_activity) > UDP_SESSION_TIMEOUT) {
            close(s->fd);
            s->active = false;
        }
    }
}

void udp_relay_destroy(udp_relay_t *relay)
{
    for (int i = 0; i < relay->session_count; i++) {
        if (relay->sessions[i].active) {
            close(relay->sessions[i].fd);
            relay->sessions[i].active = false;
        }
    }
    relay->session_count = 0;
}
