// Linux network status monitor using NETLINK_ROUTE.
// Watches for link and address changes; re-evaluates connectivity on each event.

#include "monitor_linux.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// Forward declaration of the Go-exported callback.
extern void linux_invoke_callback(uintptr_t hnd, int available, int kind);

// InterfaceKind values must match the Go constants in monitor_linux.go.
#define KIND_UNKNOWN  0
#define KIND_WIRED    1
#define KIND_WIFI     2
#define KIND_CELLULAR 3

// is_wireless returns 1 if the interface named ifname is a wireless interface.
static int is_wireless(const char *ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0) return 0;
    struct iwreq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.ifr_name, ifname, IFNAMSIZ - 1);
    int ret = ioctl(sock, SIOCGIWNAME, &req);
    close(sock);
    return ret == 0 ? 1 : 0;
}

// evaluate_status scans all network interfaces and sets *available and *kind.
// An interface is "routable" if it is UP, not loopback, not point-to-point (VPN/tunnel),
// and has at least one non-link-local IPv4 or IPv6 address.
static void evaluate_status(int *available, int *kind) {
    *available = 0;
    *kind = KIND_UNKNOWN;

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) != 0) return;

    // First pass: find any routable interface.
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (ifa->ifa_flags & IFF_POINTOPOINT) continue; // skip VPN/tunnel (e.g. tailscale)

        int af = ifa->ifa_addr->sa_family;
        if (af == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t ip = ntohl(sin->sin_addr.s_addr);
            // Skip 169.254.x.x link-local.
            if ((ip >> 16) == 0xa9fe) continue;
            *available = 1;
        } else if (af == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            // Skip link-local (fe80::/10).
            if ((sin6->sin6_addr.s6_addr[0] & 0xfe) == 0xfe &&
                (sin6->sin6_addr.s6_addr[1] & 0xc0) == 0x80) continue;
            *available = 1;
        } else {
            continue;
        }

        if (*available) {
            *kind = is_wireless(ifa->ifa_name) ? KIND_WIFI : KIND_WIRED;
            break;
        }
    }
    freeifaddrs(ifaddr);
}

typedef struct {
    int        nl_sock;
    uintptr_t  cb_hnd;
} monitor_state_t;

static void *monitor_thread(void *arg) {
    monitor_state_t *st = (monitor_state_t *)arg;
    char buf[4096];
    int prev_available = -1; // unknown initial state
    int prev_kind = KIND_UNKNOWN;

    for (;;) {
        ssize_t len = recv(st->nl_sock, buf, sizeof(buf), 0);
        if (len <= 0) break; // socket closed or error — exit thread

        struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
        int interesting = 0;
        for (; NLMSG_OK(nlh, (unsigned)len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) break;
            switch (nlh->nlmsg_type) {
            case RTM_NEWLINK: case RTM_DELLINK:
            case RTM_NEWADDR: case RTM_DELADDR:
                interesting = 1;
                break;
            }
        }
        if (!interesting) continue;

        int available, kind;
        evaluate_status(&available, &kind);
        if (available != prev_available || kind != prev_kind) {
            prev_available = available;
            prev_kind = kind;
            linux_invoke_callback(st->cb_hnd, available, kind);
        }
    }

    free(st);
    return NULL;
}

// linux_start_monitor opens a netlink socket, fires an initial status callback,
// then starts a background thread that calls linux_invoke_callback on changes.
// Returns 0 on success, -1 on error.
int linux_start_monitor(uintptr_t cb_hnd) {
    int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (sock < 0) return -1;

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    // Fire initial status synchronously before spawning the thread.
    int available, kind;
    evaluate_status(&available, &kind);
    linux_invoke_callback(cb_hnd, available, kind);

    monitor_state_t *st = malloc(sizeof(*st));
    if (!st) { close(sock); return -1; }
    st->nl_sock = sock;
    st->cb_hnd  = cb_hnd;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, monitor_thread, st) != 0) {
        pthread_attr_destroy(&attr);
        free(st);
        close(sock);
        return -1;
    }
    pthread_attr_destroy(&attr);
    return sock; // caller stores this to close on cancel
}

// linux_stop_monitor closes the netlink socket, which causes recv() in the
// monitor thread to return 0/error and the thread to exit.
void linux_stop_monitor(int sock) {
    if (sock >= 0) close(sock);
}
