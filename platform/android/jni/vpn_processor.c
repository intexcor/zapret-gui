/*
 * vpn_processor.c — Android VPN packet processor (JNI)
 *
 * Main loop: reads packets from TUN fd, dispatches TCP/UDP,
 * multiplexes relay sockets via epoll.
 *
 * Called from Java ZapretVpnService via JNI.
 */

#include "dpi_bypass.h"
#include "tcp_relay.h"
#include "udp_relay.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <jni.h>
#include <android/log.h>

#define TAG "vpn-processor"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define MAX_PKT_SIZE    65536
#define MAX_EPOLL_EVENTS 128
#define CLEANUP_INTERVAL 10  /* seconds between session cleanup */

#define IPPROTO_TCP_VAL  6
#define IPPROTO_UDP_VAL 17

static volatile int g_running = 0;
static pthread_t g_thread;

/* Global relay state (single instance — only one VPN active at a time) */
static tcp_relay_t g_tcp_relay;
static udp_relay_t g_udp_relay;

/* ------------------------------------------------------------------ */
/*  Epoll helpers                                                      */
/* ------------------------------------------------------------------ */

static int g_epoll_fd = -1;

static void epoll_add_fd(int fd)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

/* Re-register all relay fds in epoll (called periodically) */
static void epoll_refresh_relay_fds(void)
{
    int fds[TCP_MAX_SESSIONS + UDP_MAX_SESSIONS];
    int count = 0;

    count += tcp_relay_get_fds(&g_tcp_relay, fds + count, TCP_MAX_SESSIONS);
    count += udp_relay_get_fds(&g_udp_relay, fds + count, UDP_MAX_SESSIONS);

    for (int i = 0; i < count; i++) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fds[i];
        /* EPOLL_CTL_ADD may fail with EEXIST for already-added fds — that's fine */
        epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fds[i], &ev);
    }
}

/* ------------------------------------------------------------------ */
/*  Main processing loop                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int tun_fd;
    uint8_t *fake_payload;
    int fake_len;
    int fake_ttl;
    int fake_repeats;
    int split_pos;
    bool use_disorder;
    JavaVM *jvm;
    jobject vpn_service_global;
} vpn_thread_args_t;

static void *vpn_thread_func(void *arg)
{
    vpn_thread_args_t *args = (vpn_thread_args_t *)arg;

    /* Attach this thread to JVM */
    JNIEnv *env;
    JavaVM *jvm = args->jvm;
    if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != 0) {
        LOGE("Failed to attach thread to JVM");
        free(args->fake_payload);
        free(args);
        return NULL;
    }

    int tun_fd = args->tun_fd;

    LOGI("VPN processor starting: tun_fd=%d, split_pos=%d, disorder=%d, "
         "fake_ttl=%d, fake_repeats=%d, fake_len=%d",
         tun_fd, args->split_pos, args->use_disorder,
         args->fake_ttl, args->fake_repeats, args->fake_len);

    /* Initialize relays */
    tcp_relay_init(&g_tcp_relay, tun_fd,
                   args->split_pos, args->use_disorder,
                   env, args->vpn_service_global);
    udp_relay_init(&g_udp_relay, tun_fd,
                   args->fake_payload, args->fake_len,
                   args->fake_ttl, args->fake_repeats,
                   env, args->vpn_service_global);

    /* Create epoll */
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) {
        LOGE("epoll_create1: %s", strerror(errno));
        goto cleanup;
    }

    /* Add TUN fd to epoll */
    epoll_add_fd(tun_fd);

    uint8_t buf[MAX_PKT_SIZE];
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int64_t last_cleanup = 0;

    while (g_running) {
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EPOLL_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOGE("epoll_wait: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == tun_fd) {
                /* Read packet from TUN */
                ssize_t n = read(tun_fd, buf, sizeof(buf));
                if (n <= 0) continue;

                /* Parse IPv4 */
                dpi_ip_info_t ip;
                if (dpi_parse_ipv4(buf, (int)n, &ip) < 0)
                    continue;

                if (ip.protocol == IPPROTO_TCP_VAL) {
                    dpi_tcp_info_t tcp;
                    if (dpi_parse_tcp(ip.l4_data, ip.l4_len, &tcp) < 0)
                        continue;

                    tcp_relay_process(&g_tcp_relay,
                                     ip.src_addr, ip.dst_addr,
                                     tcp.src_port, tcp.dst_port,
                                     tcp.seq, tcp.ack,
                                     tcp.flags,
                                     tcp.payload, tcp.payload_len);

                    /* New session may have been created — refresh epoll */
                    epoll_refresh_relay_fds();

                } else if (ip.protocol == IPPROTO_UDP_VAL) {
                    dpi_udp_info_t udp;
                    if (dpi_parse_udp(ip.l4_data, ip.l4_len, &udp) < 0)
                        continue;

                    udp_relay_process(&g_udp_relay,
                                     ip.src_addr, ip.dst_addr,
                                     udp.src_port, udp.dst_port,
                                     udp.payload, udp.payload_len);

                    epoll_refresh_relay_fds();
                }
            } else {
                /* Response from a relay socket */
                int handled = tcp_relay_handle_response(&g_tcp_relay, fd);
                if (handled == 0)
                    handled = udp_relay_handle_response(&g_udp_relay, fd);

                if (handled < 0) {
                    /* Socket closed/error — remove from epoll */
                    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                }
            }
        }

        /* Periodic cleanup */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        if (ts.tv_sec - last_cleanup >= CLEANUP_INTERVAL) {
            tcp_relay_cleanup(&g_tcp_relay);
            udp_relay_cleanup(&g_udp_relay);
            last_cleanup = ts.tv_sec;
        }
    }

cleanup:
    LOGI("VPN processor stopping");

    tcp_relay_destroy(&g_tcp_relay);
    udp_relay_destroy(&g_udp_relay);

    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }

    /* Delete global ref */
    (*env)->DeleteGlobalRef(env, args->vpn_service_global);

    free(args->fake_payload);
    free(args);

    (*jvm)->DetachCurrentThread(jvm);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  JNI entry points                                                   */
/* ------------------------------------------------------------------ */

JNIEXPORT void JNICALL
Java_com_zapretgui_ZapretVpnService_nativeStart(JNIEnv *env, jobject thiz,
                                                  int tun_fd,
                                                  jbyteArray fake_payload_arr,
                                                  int fake_ttl, int fake_repeats,
                                                  int split_pos, jboolean use_disorder)
{
    if (g_running) {
        LOGE("VPN processor already running");
        return;
    }

    vpn_thread_args_t *args = calloc(1, sizeof(vpn_thread_args_t));
    if (!args) {
        LOGE("calloc failed");
        return;
    }

    args->tun_fd      = tun_fd;
    args->fake_ttl    = fake_ttl;
    args->fake_repeats = fake_repeats;
    args->split_pos   = split_pos;
    args->use_disorder = use_disorder;

    /* Copy fake payload from Java byte[] */
    if (fake_payload_arr != NULL) {
        args->fake_len = (*env)->GetArrayLength(env, fake_payload_arr);
        if (args->fake_len > 0) {
            args->fake_payload = malloc(args->fake_len);
            if (args->fake_payload) {
                (*env)->GetByteArrayRegion(env, fake_payload_arr, 0,
                                           args->fake_len,
                                           (jbyte *)args->fake_payload);
            }
        }
    }

    /* Get JavaVM for thread attachment */
    (*env)->GetJavaVM(env, &args->jvm);

    /* Create global ref to VpnService for JNI callbacks from worker thread */
    args->vpn_service_global = (*env)->NewGlobalRef(env, thiz);

    g_running = 1;

    if (pthread_create(&g_thread, NULL, vpn_thread_func, args) != 0) {
        LOGE("pthread_create failed: %s", strerror(errno));
        g_running = 0;
        (*env)->DeleteGlobalRef(env, args->vpn_service_global);
        free(args->fake_payload);
        free(args);
    }
}

JNIEXPORT void JNICALL
Java_com_zapretgui_ZapretVpnService_nativeStop(JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;

    if (!g_running)
        return;

    LOGI("Stopping VPN processor...");
    g_running = 0;

    /* Wait for thread to finish */
    pthread_join(g_thread, NULL);
    LOGI("VPN processor stopped");
}
