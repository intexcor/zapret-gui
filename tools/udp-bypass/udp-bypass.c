/*
 * udp-bypass — macOS UDP/QUIC DPI bypass via utun + raw socket
 *
 * Creates a utun interface, reads routed UDP packets, injects fake QUIC
 * Initial packets with low TTL before forwarding the original.
 *
 * PF rule directs traffic here:
 *   pass out route-to utunN inet proto udp from any to any port 443 user $USER
 *
 * Loop prevention: raw socket packets are marked with TOS 0x04.
 * PF has "pass out quick proto udp tos 0x04" to let them through.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <stdbool.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if.h>
#include <net/if_utun.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

#define MAX_PKT_SIZE          65536
#define UTUN_AF_HDR_LEN       4
#define DEFAULT_FAKE_TTL      3
#define DEFAULT_REPEATS       6
#define MAX_FAKE_PAYLOAD_SIZE 4096
#define PID_FILE              "/tmp/udp-bypass.pid"

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/*  CLI argument parsing helper                                        */
/* ------------------------------------------------------------------ */

static int parse_int_arg(const char *str, int min_val, int max_val, const char *name)
{
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || val < min_val || val > max_val) {
        fprintf(stderr, "Invalid %s: '%s' (must be %d..%d)\n",
                name, str, min_val, max_val);
        exit(1);
    }
    return (int)val;
}

/* ------------------------------------------------------------------ */
/*  PID file for single-instance enforcement                           */
/* ------------------------------------------------------------------ */

static bool check_and_write_pidfile(void)
{
    FILE *pf = fopen(PID_FILE, "r");
    if (pf) {
        int old_pid = 0;
        if (fscanf(pf, "%d", &old_pid) == 1 && old_pid > 0) {
            if (kill(old_pid, 0) == 0) {
                fprintf(stderr, "Another udp-bypass is running (PID %d)\n", old_pid);
                fclose(pf);
                return false;
            }
        }
        fclose(pf);
    }

    FILE *wf = fopen(PID_FILE, "w");
    if (wf) {
        fprintf(wf, "%d\n", getpid());
        fclose(wf);
    }
    return true;
}

static void remove_pidfile(void)
{
    unlink(PID_FILE);
}

/* ------------------------------------------------------------------ */
/*  utun creation                                                      */
/* ------------------------------------------------------------------ */

static int create_utun(int unit_hint, char *ifname, size_t ifname_len)
{
    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        perror("socket(PF_SYSTEM)");
        return -1;
    }

    struct ctl_info ci;
    memset(&ci, 0, sizeof(ci));
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof(ci.ctl_name));

    if (ioctl(fd, CTLIOCGINFO, &ci) < 0) {
        perror("ioctl(CTLIOCGINFO)");
        close(fd);
        return -1;
    }

    struct sockaddr_ctl sc;
    memset(&sc, 0, sizeof(sc));
    sc.sc_id      = ci.ctl_id;
    sc.sc_len     = sizeof(sc);
    sc.sc_family  = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;

    /* Try the requested unit, scan up to 50 units */
    for (int unit = unit_hint; unit < unit_hint + 50; unit++) {
        sc.sc_unit = unit + 1; /* utunN = sc_unit - 1 */
        if (connect(fd, (struct sockaddr *)&sc, sizeof(sc)) == 0) {
            snprintf(ifname, ifname_len, "utun%d", unit);
            return fd;
        }
    }

    fprintf(stderr, "Failed to create utun interface (tried utun%d..utun%d)\n",
            unit_hint, unit_hint + 49);
    close(fd);
    return -1;
}

/* ------------------------------------------------------------------ */
/*  utun interface configuration via ioctl (replaces system() call)    */
/* ------------------------------------------------------------------ */

static int configure_utun(const char *ifname)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket(AF_INET) for ifconfig");
        return -1;
    }

    struct ifreq ifr;
    struct sockaddr_in *sin;

    /* Set local address: 10.66.0.1 */
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr.s_addr = inet_addr("10.66.0.1");
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        perror("ioctl(SIOCSIFADDR 10.66.0.1)");
        close(sock);
        return -1;
    }

    /* Set destination address: 10.66.0.2 (point-to-point peer) */
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr.s_addr = inet_addr("10.66.0.2");
    if (ioctl(sock, SIOCSIFDSTADDR, &ifr) < 0) {
        perror("ioctl(SIOCSIFDSTADDR 10.66.0.2)");
        close(sock);
        return -1;
    }

    /* Bring interface up */
    memset(&ifr, 0, sizeof(ifr));
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        perror("ioctl(SIOCGIFFLAGS)");
        close(sock);
        return -1;
    }
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        perror("ioctl(SIOCSIFFLAGS IFF_UP)");
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  raw socket                                                         */
/* ------------------------------------------------------------------ */

/*
 * Use IPPROTO_UDP raw socket WITHOUT IP_HDRINCL.
 * The kernel constructs the IP header for us — we only provide
 * UDP header + payload. This avoids macOS ip_len/ip_off byte-order
 * issues with IP_HDRINCL entirely.
 *
 * TTL is controlled per-packet via setsockopt(IP_TTL).
 * Source IP is chosen by the kernel based on the routing table.
 */
static int create_raw_socket(void)
{
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (fd < 0) {
        perror("socket(SOCK_RAW, IPPROTO_UDP)");
        return -1;
    }

    /* Mark all outgoing packets with TOS 0x04 for PF loop prevention.
     * PF has a "pass out quick proto udp tos 0x04" rule that lets our
     * packets through without route-to redirection back to utun.
     *
     * THIS IS CRITICAL — without TOS marking, packets loop infinitely
     * between PF route-to and this raw socket. */
    int tos = 0x04;
    if (setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
        perror("setsockopt(IP_TOS) — FATAL: loop prevention will not work");
        close(fd);
        return -1;
    }

    return fd;
}

/* ------------------------------------------------------------------ */
/*  fake payload loading                                               */
/* ------------------------------------------------------------------ */

static uint8_t *load_fake_payload(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open fake payload: %s: %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "fseek(END) failed on %s: %s\n", path, strerror(errno));
        fclose(f);
        return NULL;
    }

    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "ftell failed on %s: %s\n", path, strerror(errno));
        fclose(f);
        return NULL;
    }
    if (len == 0 || len > MAX_FAKE_PAYLOAD_SIZE) {
        fprintf(stderr, "Invalid fake payload size: %ld (must be 1..%d)\n",
                len, MAX_FAKE_PAYLOAD_SIZE);
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "fseek(SET) failed on %s: %s\n", path, strerror(errno));
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        fprintf(stderr, "malloc(%ld) failed for fake payload\n", len);
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "Failed to read fake payload from %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------------ */
/*  QUIC detection                                                     */
/* ------------------------------------------------------------------ */

static bool is_quic_initial(const uint8_t *payload, int len)
{
    if (len < 5)
        return false;

    /* Long header (bit 7 set) */
    if ((payload[0] & 0x80) == 0)
        return false; /* short header */

    /* Check for Long Header form: first byte 0xC0..0xFF */
    uint32_t version;
    memcpy(&version, payload + 1, 4);
    version = ntohl(version);

    /* QUIC v1 or v2 */
    return version == 0x00000001 || version == 0x6b3343cf;
}

/* ------------------------------------------------------------------ */
/*  Send UDP data via raw socket                                       */
/* ------------------------------------------------------------------ */

/*
 * Send UDP data (UDP header + payload) via raw socket.
 * The kernel adds the IP header automatically.
 * TTL is set via setsockopt before each send.
 */
static int send_udp_raw(int raw_fd, const uint8_t *udp_data, int udp_len,
                        struct in_addr dst_addr, int ttl, bool verbose)
{
    if (udp_len < (int)sizeof(struct udphdr))
        return -1;

    if (setsockopt(raw_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        if (verbose)
            fprintf(stderr, "setsockopt(IP_TTL=%d): %s\n", ttl, strerror(errno));
        return -1;
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_len    = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_addr   = dst_addr;

    ssize_t sent = sendto(raw_fd, udp_data, udp_len, 0,
                          (struct sockaddr *)&dst, sizeof(dst));
    if (sent < 0) {
        /* Log unreachable errors only in verbose mode */
        if (verbose || (errno != ENETUNREACH && errno != EHOSTUNREACH))
            fprintf(stderr, "sendto: %s (errno=%d, len=%d, ttl=%d)\n",
                    strerror(errno), errno, udp_len, ttl);
    }
    return (int)sent;
}

/* ------------------------------------------------------------------ */
/*  Build and send fake packets                                        */
/* ------------------------------------------------------------------ */

static void send_fake_packets(int raw_fd,
                              struct in_addr dst_addr,
                              const struct udphdr *orig_udp,
                              const uint8_t *fake_payload, size_t fake_len,
                              int fake_ttl, int repeats,
                              bool verbose)
{
    /* Build fake UDP packet: UDP header + fake payload */
    int fake_udp_len = (int)(sizeof(struct udphdr) + fake_len);
    uint8_t fake_pkt[sizeof(struct udphdr) + MAX_FAKE_PAYLOAD_SIZE];

    /* Construct UDP header */
    struct udphdr *fake_udph = (struct udphdr *)fake_pkt;
    fake_udph->uh_sport = orig_udp->uh_sport;
    fake_udph->uh_dport = orig_udp->uh_dport;
    fake_udph->uh_ulen  = htons(fake_udp_len);
    fake_udph->uh_sum   = 0; /* UDP checksum optional for IPv4 */

    /* Copy fake payload */
    memcpy(fake_pkt + sizeof(struct udphdr), fake_payload, fake_len);

    for (int i = 0; i < repeats; i++) {
        send_udp_raw(raw_fd, fake_pkt, fake_udp_len, dst_addr, fake_ttl, verbose);
    }

    if (verbose) {
        char dst_s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &dst_addr, dst_s, sizeof(dst_s));
        fprintf(stderr, "udp-bypass:fake x%d TTL=%d -> %s:%d\n",
                repeats, fake_ttl,
                dst_s, ntohs(orig_udp->uh_dport));
    }
}

/* ------------------------------------------------------------------ */
/*  Main loop                                                          */
/* ------------------------------------------------------------------ */

static void main_loop(int utun_fd, int raw_fd,
                      const uint8_t *fake_payload, size_t fake_len,
                      int fake_ttl, int repeats, bool verbose)
{
    uint8_t buf[MAX_PKT_SIZE];

    struct pollfd pfd = {
        .fd     = utun_fd,
        .events = POLLIN
    };

    while (g_running) {
        /* poll() with 1s timeout — no FD_SETSIZE limit unlike select() */
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (ret == 0)
            continue; /* timeout — check g_running */

        ssize_t nread = read(utun_fd, buf, sizeof(buf));
        if (nread < 0) {
            if (errno == EINTR)
                continue;
            perror("read(utun)");
            break;
        }

        if (nread < UTUN_AF_HDR_LEN)
            continue;

        /* Check AF header — we only handle IPv4 */
        uint32_t af;
        memcpy(&af, buf, 4);
        af = ntohl(af);
        if (af != AF_INET)
            continue;

        const uint8_t *ip_pkt = buf + UTUN_AF_HDR_LEN;
        int ip_pkt_len = (int)(nread - UTUN_AF_HDR_LEN);

        if (ip_pkt_len < (int)sizeof(struct ip))
            continue;

        const struct ip *iph = (const struct ip *)ip_pkt;

        /* Validate IP header length BEFORE accessing further fields */
        int ip_hlen = iph->ip_hl * 4;
        if (ip_hlen < (int)sizeof(struct ip)) {
            if (verbose)
                fprintf(stderr, "udp-bypass:skip malformed IP hlen=%d\n", ip_hlen);
            continue;
        }

        if (iph->ip_p != IPPROTO_UDP)
            continue;

        if (ip_pkt_len < ip_hlen + (int)sizeof(struct udphdr))
            continue;

        const struct udphdr *udph = (const struct udphdr *)(ip_pkt + ip_hlen);
        const uint8_t *udp_raw = ip_pkt + ip_hlen; /* UDP header + payload */
        int udp_raw_len = ip_pkt_len - ip_hlen;
        int udp_payload_off = ip_hlen + (int)sizeof(struct udphdr);
        int udp_payload_len = ip_pkt_len - udp_payload_off;

        /* Original TTL from the captured packet */
        int orig_ttl = iph->ip_ttl;
        struct in_addr dst_addr = iph->ip_dst;

        /* Safety net: skip packets with very low TTL — likely our own fakes
         * re-captured (should not happen with TOS marking, but just in case). */
        if (orig_ttl > 0 && orig_ttl <= fake_ttl) {
            if (verbose)
                fprintf(stderr, "udp-bypass:skip looped pkt TTL=%d\n", orig_ttl);
            continue;
        }

        if (verbose) {
            char src[INET_ADDRSTRLEN], dst_s[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &iph->ip_src, src, sizeof(src));
            inet_ntop(AF_INET, &dst_addr, dst_s, sizeof(dst_s));
            fprintf(stderr, "udp-bypass:pkt %s:%d -> %s:%d len=%d ttl=%d\n",
                    src, ntohs(udph->uh_sport),
                    dst_s, ntohs(udph->uh_dport),
                    udp_payload_len, orig_ttl);
        }

        /* If QUIC Initial detected and we have a fake payload, inject fakes */
        if (fake_payload && fake_len > 0 && udp_payload_len > 0) {
            const uint8_t *udp_data = ip_pkt + udp_payload_off;
            if (is_quic_initial(udp_data, udp_payload_len)) {
                if (verbose)
                    fprintf(stderr, "udp-bypass:QUIC Initial detected, injecting fakes\n");
                send_fake_packets(raw_fd, dst_addr, udph,
                                  fake_payload, fake_len,
                                  fake_ttl, repeats, verbose);
            }
        }

        /* Forward the original packet via raw socket (UDP header + payload) */
        send_udp_raw(raw_fd, udp_raw, udp_raw_len, dst_addr, orig_ttl, verbose);
    }
}

/* ------------------------------------------------------------------ */
/*  Usage / CLI                                                        */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --fake-quic <file>   Fake QUIC Initial payload (.bin)\n"
        "  --fake-ttl <N>       TTL for fake packets (default: %d, range: 1-255)\n"
        "  --repeats <N>        Number of fake packet repeats (default: %d, range: 1-100)\n"
        "  --utun-start <N>     Starting utun unit number to try (default: 20, range: 0-255)\n"
        "  --verbose            Enable debug logging\n"
        "  --help               Show this help\n",
        prog, DEFAULT_FAKE_TTL, DEFAULT_REPEATS);
}

int main(int argc, char *argv[])
{
    const char *fake_quic_path = NULL;
    int fake_ttl   = DEFAULT_FAKE_TTL;
    int repeats    = DEFAULT_REPEATS;
    int utun_start = 20;
    bool verbose   = false;

    static struct option long_opts[] = {
        { "fake-quic",  required_argument, NULL, 'q' },
        { "fake-ttl",   required_argument, NULL, 't' },
        { "repeats",    required_argument, NULL, 'r' },
        { "utun-start", required_argument, NULL, 'u' },
        { "verbose",    no_argument,       NULL, 'v' },
        { "help",       no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "q:t:r:u:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'q': fake_quic_path = optarg; break;
        case 't': fake_ttl   = parse_int_arg(optarg, 1, 255, "fake-ttl"); break;
        case 'r': repeats    = parse_int_arg(optarg, 1, 100, "repeats"); break;
        case 'u': utun_start = parse_int_arg(optarg, 0, 255, "utun-start"); break;
        case 'v': verbose = true; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Error: udp-bypass must run as root\n");
        return 1;
    }

    /* Single-instance check via PID file */
    if (!check_and_write_pidfile())
        return 1;

    /* Ignore SIGPIPE — GUI parent may close our stdout pipe on crash */
    signal(SIGPIPE, SIG_IGN);

    /* Install signal handlers using sigaction() for reliable behavior.
     * signal() may reset the handler after first delivery on some systems. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Load fake payload */
    uint8_t *fake_payload = NULL;
    size_t fake_len = 0;
    if (fake_quic_path) {
        fake_payload = load_fake_payload(fake_quic_path, &fake_len);
        if (!fake_payload) {
            remove_pidfile();
            return 1;
        }
        if (verbose)
            fprintf(stderr, "udp-bypass:Loaded fake QUIC payload: %zu bytes\n", fake_len);
    }

    /* Create utun interface */
    char ifname[32];
    int utun_fd = create_utun(utun_start, ifname, sizeof(ifname));
    if (utun_fd < 0) {
        free(fake_payload);
        remove_pidfile();
        return 1;
    }

    /* Configure the utun interface with point-to-point addresses via ioctl.
     * PF route-to requires the interface to have an address assigned.
     * We use 10.66.0.1/10.66.0.2 — a private range unlikely to conflict. */
    if (configure_utun(ifname) < 0) {
        fprintf(stderr, "Failed to configure interface %s\n", ifname);
        close(utun_fd);
        free(fake_payload);
        remove_pidfile();
        return 1;
    }

    /* Print interface name for the GUI to parse (must be on stdout) */
    printf("UTUN:%s\n", ifname);
    fflush(stdout);

    if (verbose)
        fprintf(stderr, "udp-bypass: Created interface %s (10.66.0.1/10.66.0.2)\n", ifname);

    /* Create raw socket for sending packets */
    int raw_fd = create_raw_socket();
    if (raw_fd < 0) {
        close(utun_fd);
        free(fake_payload);
        remove_pidfile();
        return 1;
    }

    fprintf(stderr, "udp-bypass:Running on %s, fake_ttl=%d, repeats=%d\n",
            ifname, fake_ttl, repeats);

    /* Enter main loop */
    main_loop(utun_fd, raw_fd, fake_payload, fake_len, fake_ttl, repeats, verbose);

    fprintf(stderr, "udp-bypass:Shutting down\n");

    close(raw_fd);
    close(utun_fd);
    free(fake_payload);
    remove_pidfile();

    return 0;
}
