#!/usr/bin/env python3
"""Host tests for 0080-sweet-net-trace.patch (ASan/UBSan + TSan)."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE', '1')

ROOT = Path(__file__).resolve().parents[1]
PATCH_0080 = ROOT / 'xnu/patches/0080-sweet-net-trace.patch'

HARNESS_SRC = r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define ARM64_BOARD_CONFIG_SWEET 1
#define kSuccess 0
#define TRUE 1
#define FALSE 0
#define Z_WAITOK 0
#define AF_INET 2
#define AF_INET6 30
#define PF_ROUTE 17
#define RTM_ADD 0x1
#define RTM_DELETE 0x2
#define RTM_CHANGE 0x3
#define RTAX_DST 0
#define RTAX_GATEWAY 1
#define RTAX_NETMASK 2
#define RTAX_MAX 8
#define IFSCOPE_NONE 0
#define IFF_POINTOPOINT 0x10
#define IFF_LOOPBACK 0x8
#define IFF_BROADCAST 0x2
#define IFD_ATTACHED 0x1
#define IFD_DETACHING 0x2
#define IFD_NOTREADY 0x4
#define INADDR_ANY 0x00000000u
#define INADDR_BROADCAST 0xffffffffu
#define IN_MULTICAST(i) (((uint32_t)(i) & 0xf0000000u) == 0xe0000000u)
#define htonl(x) __builtin_bswap32((uint32_t)(x))
#define ntohl(x) __builtin_bswap32((uint32_t)(x))
#define htons(x) __builtin_bswap16((uint16_t)(x))
#define ntohs(x) __builtin_bswap16((uint16_t)(x))
#define OS_FALLTHROUGH __attribute__((fallthrough))
#define __unused __attribute__((unused))

#define SIOCGASSOCIDS32 0x1001UL
#define SIOCGASSOCIDS64 0x1002UL
#define SIOCGCONNIDS32  0x1003UL
#define SIOCGCONNIDS64  0x1004UL
#define SIOCGCONNINFO32 0x1005UL
#define SIOCGCONNINFO64 0x1006UL
#define SIOCAUTOADDR    0x1007UL
#define SIOCSIFADDR     0x2001UL
#define SIOCSIFDSTADDR  0x2002UL
#define SIOCSIFNETMASK  0x2003UL
#define SIOCAIFADDR     0x2004UL
#define SIOCDIFADDR     0x2005UL
#define SIOCGIFDSTADDR  0x2006UL
#define SIOCGIFBRDADDR  0x2007UL
#define SIOCSIFBRDADDR  0x200aUL
#define SIOCGIFNETMASK  0x2008UL
#define SIOCGIFADDR     0x2009UL

typedef int boolean_t;
typedef unsigned long u_long;
typedef unsigned int u_int;
typedef uint32_t u_int32_t;
typedef char *caddr_t;
typedef struct fuxi_dt_entry *DTEntry;

struct in_addr { uint32_t s_addr; };
struct sockaddr { uint8_t sa_len; uint8_t sa_family; char sa_data[14]; };
struct sockaddr_in { uint8_t sin_len; uint8_t sin_family; uint16_t sin_port; struct in_addr sin_addr; char sin_zero[8]; };
struct sockaddr_in6 { uint8_t sin6_len; uint8_t sin6_family; uint16_t sin6_port; uint32_t sin6_flowinfo; struct { uint16_t s6_addr16[8]; } sin6_addr; uint32_t sin6_scope_id; };

#define SIN(s) ((struct sockaddr_in *)(void *)(s))
#define SIN6(s) ((struct sockaddr_in6 *)(void *)(s))

struct ifreq {
    char ifr_name[16];
    struct sockaddr_in ifr_addr;
};
struct in_aliasreq {
    char ifra_name[16];
    struct sockaddr_in ifra_addr;
    struct sockaddr_in ifra_broadaddr;
    struct sockaddr_in ifra_mask;
    struct sockaddr_in ifra_dstaddr;
};
struct ifnet {
    char if_xname[16];
    uint32_t if_flags;
};
struct ifaddr {
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_dstaddr;
    struct sockaddr *ifa_netmask;
    uint32_t ifa_debug;
    int ifa_del_waiters;
    void *ifa_del_wc;
    int ifa_lock;
};
struct in_ifaddr {
    struct ifaddr ia_ifa;
    struct sockaddr_in ia_addr;
    struct sockaddr_in ia_dstaddr;
    struct sockaddr_in ia_sockmask;
    struct sockaddr_in ia_broadaddr;
    struct ifnet *ia_ifp;
    int ia_link;
};
struct socket { int dummy; };
struct proc { int pid; char name[32]; int is_suser; };
struct rt_msghdr { uint8_t rtm_type; uint16_t rtm_index; uint32_t rtm_flags; };
struct rt_addrinfo { struct sockaddr *rti_info[RTAX_MAX]; };
struct route_entry { int dummy; };
struct rtstat { int dummy; };
struct domain { int dummy; };
static struct rtstat rtstat;
static struct domain routedomain_s;

static struct proc g_kernproc = { 0, "kernel_task", 1 };
static struct proc *kernproc = &g_kernproc;
static _Thread_local struct proc *tl_proc = &g_kernproc;

static int proc_pid(struct proc *p) { return p ? p->pid : 0; }
static int proc_suser(struct proc *p) { return (p && p->is_suser) ? 0 : EPERM; }
static void proc_name(int pid, char *buf, int sz) {
    (void)pid;
    if (sz > 0) {
        const char *n = tl_proc ? tl_proc->name : "?";
        snprintf(buf, (size_t)sz, "%s", n);
    }
}
static void proc_selfname(char *buf, int sz) { proc_name(0, buf, sz); }
static const char *if_name(struct ifnet *ifp) { return ifp ? ifp->if_xname : "?"; }

static int mock_has_bootarg = 0;
static uint32_t mock_bootarg_val = 0;
static int mock_has_adt = 0;
static uint32_t mock_adt_val = 0;
static unsigned int mock_adt_len = 4;
static struct fuxi_dt_entry { int id; } g_chosen;

static bool PE_parse_boot_argn(const char *name, void *val, int size) {
    if (name && strcmp(name, "sweetnettrace") == 0 && mock_has_bootarg && size == (int)sizeof(uint32_t)) {
        memcpy(val, &mock_bootarg_val, sizeof(uint32_t));
        return true;
    }
    return false;
}
static int SecureDTLookupEntry(const DTEntry *s, const char *p, DTEntry *out) {
    (void)s;
    if (p && strcmp(p, "/chosen") == 0) { *out = &g_chosen; return kSuccess; }
    return -1;
}
static int SecureDTGetProperty(DTEntry e, const char *p, void const **v, unsigned int *sz) {
    if (e == &g_chosen && p && strcmp(p, "fuxi,net-trace-filter") == 0 && mock_has_adt) {
        *v = &mock_adt_val;
        *sz = mock_adt_len;
        return kSuccess;
    }
    return -1;
}

static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_log_count = 0;
static char g_logs[512][192];
static int mock_printf(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&g_log_mtx);
    if (g_log_count < 512) {
        snprintf(g_logs[g_log_count], sizeof(g_logs[0]), "%s", buf);
    }
    g_log_count++;
    pthread_mutex_unlock(&g_log_mtx);
    return 0;
}
#define printf mock_printf

#define VERIFY(x) assert(x)
#define bcopy(s, d, n) memcpy(d, s, n)
#define IN6_IS_SCOPE_EMBED(a) 0
#define IN6_IS_ADDR_UNICAST_BASED_MULTICAST(a) 0
__unused static void sin_set_ifscope(struct sockaddr *sa, int sc) { (void)sa; (void)sc; }
static int rtrequest_scoped_locked(int a, struct sockaddr *b, struct sockaddr *c, struct sockaddr *d, uint32_t e, struct route_entry **f, int g) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
    return 0;
}
#define senderr(e) do { (void)(e); return (e); } while (0)
__unused static void RT_LOCK(struct route_entry *r) { (void)r; }

static int g_in_ifaddr_rwlock = 0;
static void lck_rw_lock_shared(int *l) { (void)l; }
static void lck_rw_lock_exclusive(int *l) { (void)l; }
static void lck_rw_done(int *l) { (void)l; }
static void IFA_LOCK(struct ifaddr *a) { (void)a; }
static void IFA_UNLOCK(struct ifaddr *a) { (void)a; }
static void IFA_ADDREF(struct ifaddr *a) { (void)a; }
static void IFA_ADDREF_LOCKED(struct ifaddr *a) { (void)a; }
static void IFA_REMREF(struct ifaddr *a) { (void)a; }
__unused static void IFA_LOCK_ASSERT_HELD(struct ifaddr *a) { (void)a; }
static void ifnet_lock_exclusive(struct ifnet *i) { (void)i; }
static void ifnet_lock_done(struct ifnet *i) { (void)i; }
static void if_attach_ifa(struct ifnet *i, struct ifaddr *a) { (void)i; (void)a; }
static int in_domifattach(struct ifnet *i) { (void)i; return 0; }
static void socket_lock(struct socket *s, int a) { (void)s; (void)a; }
__unused static const char *inet_ntoa(struct in_addr a) { (void)a; return "0.0.0.0"; }
static int msleep(void *a, int *b, int c, const char *d, void *e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static void wakeup(void *a) { (void)a; }
__unused static void os_log(int a, const char *f, ...) { (void)a; (void)f; }
#define OS_LOG_DEFAULT 0
#define PZERO 0
#define IA_HASH_INIT(ia) do { (void)(ia); } while (0)
struct { int dummy; } in_ifaddrhead;
#define TAILQ_INSERT_TAIL(h, e, f) do { (void)(h); (void)(e); } while (0)
static int in_interfaces = 0;
static struct in_ifaddr *mock_found_ia = NULL;
static int mock_alloc_fail = 0;
static int mock_inctl_ret = 0;
static struct in_ifaddr g_mock_ia_storage;
static struct in_ifaddr *in_ifaddr_alloc(int fl) {
    (void)fl;
    if (mock_alloc_fail) return NULL;
    memset(&g_mock_ia_storage, 0, sizeof(g_mock_ia_storage));
    return &g_mock_ia_storage;
}
static int inctl_associd(struct socket *s, u_long c, caddr_t d) { (void)s; (void)c; (void)d; return 0; }
static int inctl_connid(struct socket *s, u_long c, caddr_t d) { (void)s; (void)c; (void)d; return 0; }
static int inctl_conninfo(struct socket *s, u_long c, caddr_t d) { (void)s; (void)c; (void)d; return 0; }
static int inctl_ifdstaddr(struct ifnet *i, struct in_ifaddr *ia, u_long c, struct ifreq *r) { (void)i; (void)ia; (void)c; (void)r; return 0; }
static int inctl_ifbrdaddr(struct ifnet *i, struct in_ifaddr *ia, u_long c, struct ifreq *r) { (void)i; (void)ia; (void)c; (void)r; return 0; }
static int inctl_ifnetmask(struct ifnet *i, struct in_ifaddr *ia, u_long c, struct ifreq *r) { (void)i; (void)ia; (void)c; (void)r; return 0; }
static int inctl_ifaddr(struct ifnet *i, struct in_ifaddr *ia, u_long c, struct ifreq *r) {
    (void)i; (void)ia; (void)c; (void)r;
    return mock_inctl_ret;
}

#include "bsd/net/rtsock.c"
#include "bsd/netinet/in.c"
#undef printf

static void make_req(struct in_aliasreq *ar, uint32_t ip_host, uint32_t dst_host, uint8_t fam) {
    memset(ar, 0, sizeof(*ar));
    snprintf(ar->ifra_name, sizeof(ar->ifra_name), "en0");
    ar->ifra_addr.sin_len = sizeof(ar->ifra_addr);
    ar->ifra_addr.sin_family = fam;
    ar->ifra_addr.sin_addr.s_addr = htonl(ip_host);
    ar->ifra_dstaddr.sin_len = sizeof(ar->ifra_dstaddr);
    ar->ifra_dstaddr.sin_family = AF_INET;
    ar->ifra_dstaddr.sin_addr.s_addr = htonl(dst_host);
}

static void invoke_rt(uint8_t rtm_type, uint32_t dst_host) {
    struct rt_msghdr rtm;
    memset(&rtm, 0, sizeof(rtm));
    rtm.rtm_type = rtm_type;
    rtm.rtm_index = 5;
    rtm.rtm_flags = 0x1;
    struct sockaddr_in dst, gw;
    memset(&dst, 0, sizeof(dst));
    memset(&gw, 0, sizeof(gw));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(dst_host);
    gw.sin_family = AF_INET;
    gw.sin_addr.s_addr = htonl(0xc0a80101u);
    struct rt_addrinfo info;
    memset(&info, 0, sizeof(info));
    info.rti_info[RTAX_DST] = (struct sockaddr *)&dst;
    info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&gw;
    (void)mock_rtsock_step(&rtm, info);
}

static void *tsan_worker(void *arg) {
    int id = *(int *)arg;
    struct proc p = { 100 + id, "configd", 1 };
    tl_proc = &p;
    struct socket so = {0};
    struct ifnet ifp = { "en0", IFF_BROADCAST };
    struct in_aliasreq ar;
    make_req(&ar, 0xc0000002u, 0, AF_INET);
    for (int i = 0; i < 40; i++) {
        (void)in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &p);
        invoke_rt(RTM_ADD, 0x0a000001u);
    }
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];
    struct proc priv_p = { 77, "IPConfiguration", 1 };
    struct proc unpriv_p = { 78, "nobody", 0 };
    struct socket so = {0};
    struct ifnet ifp = { "en0", IFF_BROADCAST };
    struct ifnet ppp_ifp = { "ppp0", IFF_POINTOPOINT };
    struct in_aliasreq ar;

    if (strcmp(mode, "--mode-0") == 0) {
        mock_has_adt = 1; mock_adt_val = 0; mock_adt_len = 4;
        tl_proc = &priv_p;
        for (int i = 0; i < 120; i++) {
            make_req(&ar, 0xc0000002u, 0, AF_INET);
            mock_found_ia = NULL;
            assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        }
        assert(g_log_count == 100);
        assert(strstr(g_logs[0], "SWEET NET: IPConfiguration en0 delete address 192.0.0.2") != NULL);
        /* Once 100-slot quota is exhausted in mode 0, subsequent SIOCAIFADDR is starved */
        make_req(&ar, 0xc000020au, 0, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == 0);
        assert(g_log_count == 100);
        invoke_rt(RTM_ADD, 0x0a000001u);
        assert(g_log_count == 101);
        puts("net_trace mode 0 ok");
        return 0;
    }

    if (strcmp(mode, "--mode-1-adt") == 0 || strcmp(mode, "--mode-1-bootarg") == 0) {
        if (strcmp(mode, "--mode-1-adt") == 0) {
            mock_has_adt = 1; mock_adt_val = 1; mock_adt_len = 4;
        } else {
            mock_has_adt = 1; mock_adt_val = 2; mock_adt_len = 4;
            mock_has_bootarg = 1; mock_bootarg_val = 1;
        }
        tl_proc = &priv_p;

        /* Path 1: ifp == NULL -> ENXIO, 0 logs */
        make_req(&ar, 0xc0000002u, 0, AF_INET);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, NULL, &priv_p) == ENXIO);
        /* Path 2 & 3: !privileged -> EPERM, 0 logs */
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &unpriv_p) == EPERM);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &unpriv_p) == EPERM);
        assert(g_log_count == 0);

        /* Path 6: 150x SIOCDIFADDR ia == NULL (EADDRNOTAVAIL) -> 0 logs, 0 quota used */
        for (int i = 0; i < 150; i++) {
            mock_found_ia = NULL;
            assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        }
        /* Path 7: 50x concurrent SIOCDIFADDR !(ifa_debug & IFD_ATTACHED) -> 0 logs */
        for (int i = 0; i < 50; i++) {
            struct in_ifaddr detached_ia;
            memset(&detached_ia, 0, sizeof(detached_ia));
            detached_ia.ia_ifa.ifa_debug = 0;
            mock_found_ia = &detached_ia;
            assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        }
        assert(g_log_count == 0);

        /* Valid events MUST still log and have full 100-slot quota available */
        mock_found_ia = NULL; mock_inctl_ret = 0;
        make_req(&ar, 0xc000020au, 0, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == 0);
        assert(g_log_count == 1);
        assert(strstr(g_logs[0], "SWEET NET: IPConfiguration en0 add address 192.0.2.10") != NULL);

        struct in_ifaddr live_ia;
        memset(&live_ia, 0, sizeof(live_ia));
        live_ia.ia_ifa.ifa_debug = IFD_ATTACHED;
        mock_found_ia = &live_ia;
        make_req(&ar, 0xc000020au, 0, AF_INET);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == 0);
        assert(g_log_count == 2);
        assert(strstr(g_logs[1], "SWEET NET: IPConfiguration en0 delete address 192.0.2.10") != NULL);

        /* Path 4: IFF_POINTOPOINT dst==INADDR_ANY -> EDESTADDRREQ, logs */
        mock_found_ia = NULL;
        make_req(&ar, 0x0a000001u, INADDR_ANY, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ppp_ifp, &priv_p) == EDESTADDRREQ);
        assert(g_log_count == 3);

        /* Path 5: sin_family != AF_INET -> EINVAL, logs */
        make_req(&ar, 0x0a000001u, 0, AF_INET6);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EINVAL);
        assert(g_log_count == 4);

        /* Path 8: broadcast/multicast/INADDR_ANY -> EINVAL, logs */
        make_req(&ar, INADDR_BROADCAST, 0, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EINVAL);
        assert(g_log_count == 5);

        /* Path 9: ENOBUFS -> logs */
        mock_alloc_fail = 1;
        make_req(&ar, 0x0a000002u, 0, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == ENOBUFS);
        mock_alloc_fail = 0;
        assert(g_log_count == 6);

        /* rtsock.c still logs in mode 1 */
        invoke_rt(RTM_ADD, 0x0a000001u);
        assert(g_log_count == 7);
        puts("net_trace mode 1 ok");
        return 0;
    }

    if (strcmp(mode, "--mode-2-adt") == 0 || strcmp(mode, "--mode-2-bootarg") == 0) {
        if (strcmp(mode, "--mode-2-adt") == 0) {
            mock_has_adt = 1; mock_adt_val = 2; mock_adt_len = 4;
        } else {
            mock_has_adt = 1; mock_adt_val = 0; mock_adt_len = 4;
            mock_has_bootarg = 1; mock_bootarg_val = 2;
        }
        tl_proc = &priv_p;
        mock_found_ia = NULL;
        make_req(&ar, 0xc0000002u, 0, AF_INET);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        make_req(&ar, 0xc000020au, 0, AF_INET);
        assert(in_control(&so, SIOCAIFADDR, (caddr_t)&ar, &ifp, &priv_p) == 0);
        invoke_rt(RTM_ADD, 0x0a000001u);
        invoke_rt(RTM_DELETE, 0x0a000001u);
        invoke_rt(RTM_CHANGE, 0x0a000001u);
        assert(g_log_count == 0);
        puts("net_trace mode 2 ok");
        return 0;
    }

    if (strcmp(mode, "--bad-modes") == 0) {
        mock_has_adt = 1; mock_adt_val = 3; mock_adt_len = 4;
        tl_proc = &priv_p;
        mock_found_ia = NULL;
        make_req(&ar, 0xc0000002u, 0, AF_INET);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        assert(g_log_count == 1); /* invalid value 3 safely falls back to 0 */
        puts("net_trace bad modes ok");
        return 0;
    }

    if (strcmp(mode, "--bad-len") == 0) {
        mock_has_adt = 1; mock_adt_val = 2; mock_adt_len = 2;
        tl_proc = &priv_p;
        mock_found_ia = NULL;
        make_req(&ar, 0xc0000002u, 0, AF_INET);
        assert(in_control(&so, SIOCDIFADDR, (caddr_t)&ar, &ifp, &priv_p) == EADDRNOTAVAIL);
        assert(g_log_count == 1); /* malformed length != 4 falls back to 0 */
        puts("net_trace bad len ok");
        return 0;
    }

    if (strcmp(mode, "--tsan") == 0) {
        mock_has_adt = 1; mock_adt_val = 1; mock_adt_len = 4;
        pthread_t ths[8];
        int ids[8];
        for (int i = 0; i < 8; i++) {
            ids[i] = i;
            assert(pthread_create(&ths[i], NULL, tsan_worker, &ids[i]) == 0);
        }
        for (int i = 0; i < 8; i++) {
            pthread_join(ths[i], NULL);
        }
        assert(g_log_count == 200); /* 8 * 40 rtsock capped at 200; EADDRNOTAVAIL 0 logs */
        puts("net_trace tsan ok");
        return 0;
    }

    return 2;
}
'''


def build_skeletons(t: Path) -> None:
    rt_path = t / 'bsd/net/rtsock.c'
    rt_path.parent.mkdir(parents=True, exist_ok=True)
    rt_lines = [''] * 530
    blk86 = [
        '#include <netinet/in_arp.h>',
        '#include <netinet/ip.h>',
        '#include <netinet/ip6.h>',
        '#include <netinet6/nd6.h>',
        '',
        '#include <IOKit/IOBSD.h>',
        '',
        'extern struct rtstat rtstat;',
        'extern struct domain routedomain_s;',
    ]
    for idx, val in enumerate(blk86, start=86):
        rt_lines[idx - 1] = val
    blk505 = [
        'static int mock_rtsock_step(struct rt_msghdr *rtm, struct rt_addrinfo info) {',
        '\tint error = 0;',
        '\tstruct route_entry *saved_nrt = NULL;',
        '\tint ifscope = IFSCOPE_NONE;',
        '\tinfo.rti_info[RTAX_DST] = info.rti_info[RTAX_DST];',
        '\tif (info.rti_info[RTAX_DST]->sa_family == AF_INET6 &&',
        '\t    IN6_IS_SCOPE_EMBED(&SIN6(info.rti_info[RTAX_DST])->sin6_addr) &&',
        '\t    !IN6_IS_ADDR_UNICAST_BASED_MULTICAST(&SIN6(info.rti_info[RTAX_DST])->sin6_addr) &&',
        '\t    SIN6(info.rti_info[RTAX_DST])->sin6_scope_id == 0) {',
        '\t\tSIN6(info.rti_info[RTAX_DST])->sin6_scope_id = ntohs(SIN6(info.rti_info[RTAX_DST])->sin6_addr.s6_addr16[1]);',
        '\t\tSIN6(info.rti_info[RTAX_DST])->sin6_addr.s6_addr16[1] = 0;',
        '\t}',
        '',
        '\tswitch (rtm->rtm_type) {',
        '\tcase RTM_ADD:',
        '\t\tif (info.rti_info[RTAX_GATEWAY] == NULL) {',
        '\t\t\tsenderr(EINVAL);',
        '\t\t}',
        '\t\t(void)rtrequest_scoped_locked(RTM_ADD, info.rti_info[RTAX_DST], info.rti_info[RTAX_GATEWAY], NULL, 0, &saved_nrt, ifscope);',
        '\t\tbreak;',
        '\tdefault:',
        '\t\tbreak;',
        '\t}',
        '\treturn error;',
        '}',
    ]
    for idx, val in enumerate(blk505, start=501):
        rt_lines[idx - 1] = val
    rt_path.write_text('\n'.join(rt_lines) + '\n', encoding='utf-8')

    in_path = t / 'bsd/netinet/in.c'
    in_path.parent.mkdir(parents=True, exist_ok=True)
    in_lines = [''] * 1605
    blk75 = [
        '#include <sys/protosw.h>',
        '#include <sys/file.h>',
        '',
        '#include <kern/zalloc.h>',
        '#include <pexpert/pexpert.h>',
        '#include <os/log.h>',
        '',
        '#include <net/if.h>',
        '#include <net/if_types.h>',
        '#include <net/route.h>',
    ]
    for idx, val in enumerate(blk75, start=75):
        in_lines[idx - 1] = val

    # Full 979e2f08 in_control() coverage of all 10 return paths at exact lines 1197..1591
    blk1197 = [
        'int',
        'in_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,',
        '    struct proc *p)',
        '{',
        '\tstruct ifreq *ifr = (struct ifreq *)(void *)data;',
        '\tstruct sockaddr_in addr, dstaddr;',
        '\tstruct sockaddr_in sin, *sa = NULL;',
        '\tboolean_t privileged = (proc_suser(p) == 0);',
        '\tboolean_t so_unlocked = FALSE;',
        '\tstruct in_ifaddr *ia = NULL;',
        '\tstruct ifaddr *ifa;',
        '\tint error = 0;',
        '\tint intval = 0; (void)intval; (void)sin; (void)sa;',
        '',
        '\t/* In case it\'s NULL, make sure it came from the kernel */',
        '\tVERIFY(so != NULL || p == kernproc);',
        '',
        '\t/*',
        '\t * ioctls which don\'t require ifp, but require socket.',
        '\t */',
        '\tswitch (cmd) {',
        '\tcase SIOCGASSOCIDS32:',
        '\tcase SIOCGASSOCIDS64:',
        '\t\treturn inctl_associd(so, cmd, data);',
        '\tcase SIOCGCONNIDS32:',
        '\tcase SIOCGCONNIDS64:',
        '\t\treturn inctl_connid(so, cmd, data);',
        '\tcase SIOCGCONNINFO32:',
        '\tcase SIOCGCONNINFO64:',
        '\t\treturn inctl_conninfo(so, cmd, data);',
        '\t}',
        '\tif (ifp == NULL) {',
        '\t\treturn ENXIO;',
        '\t}',
        '\tswitch (cmd) {',
        '\tcase SIOCAIFADDR:',
        '\tcase SIOCDIFADDR:',
        '\t\tif (cmd == SIOCAIFADDR) {',
        '\t\t\tif (!privileged) return EPERM;',
        '\t\t\tbcopy(&((struct in_aliasreq *)(void *)data)->ifra_addr, &addr, sizeof(addr));',
        '\t\t\tbcopy(&((struct in_aliasreq *)(void *)data)->ifra_dstaddr, &dstaddr, sizeof(dstaddr));',
        '\t\t} else {',
        '\t\t\tif (!privileged) return EPERM;',
        '\t\t\tbcopy(&((struct ifreq *)(void *)data)->ifr_addr, &addr, sizeof(addr));',
        '\t\t}',
        '\t\tif (addr.sin_family == AF_INET) {',
        '\t\t\tsa = &addr;',
        '\t\t\tlck_rw_lock_shared(&g_in_ifaddr_rwlock);',
        '\t\t\tia = mock_found_ia;',
        '\t\t\tlck_rw_done(&g_in_ifaddr_rwlock);',
        '\t\t\tif ((ifp->if_flags & IFF_POINTOPOINT) &&',
        '\t\t\t    (cmd == SIOCAIFADDR) &&',
        '\t\t\t    (dstaddr.sin_addr.s_addr == INADDR_ANY)) {',
        '\t\t\t\terror = EDESTADDRREQ;',
        '\t\t\t\tgoto done;',
        '\t\t\t}',
        '\t\t} else if (cmd == SIOCAIFADDR) {',
        '\t\t\terror = EINVAL;',
        '\t\t\tgoto done;',
        '\t\t}',
        '\t\tif (cmd == SIOCDIFADDR) {',
        '\t\t\tif (ia == NULL) {',
        '\t\t\t\terror = EADDRNOTAVAIL;',
        '\t\t\t\tgoto done;',
        '\t\t\t}',
        '\t\t\tIFA_LOCK(&ia->ia_ifa);',
        '\t\t\twhile (ia->ia_ifa.ifa_debug & IFD_DETACHING) {',
        '\t\t\t\t(void)msleep(ia->ia_ifa.ifa_del_wc, &ia->ia_ifa.ifa_lock, PZERO, __func__, NULL);',
        '\t\t\t}',
        '\t\t\tif ((ia->ia_ifa.ifa_debug & IFD_ATTACHED) == 0) {',
        '\t\t\t\terror = EADDRNOTAVAIL;',
        '\t\t\t\tIFA_UNLOCK(&ia->ia_ifa);',
        '\t\t\t\tgoto done;',
        '\t\t\t}',
        '\t\t\tia->ia_ifa.ifa_debug |= IFD_DETACHING;',
        '\t\t\tIFA_UNLOCK(&ia->ia_ifa);',
        '\t\t}',
        '\t\tOS_FALLTHROUGH;',
        '\tcase SIOCSIFADDR:',
        '\tcase SIOCSIFDSTADDR:',
        '\tcase SIOCSIFNETMASK:',
        '\t\tif ((cmd == SIOCAIFADDR || cmd == SIOCSIFADDR) &&',
        '\t\t    (IN_MULTICAST(ntohl(addr.sin_addr.s_addr)) ||',
        '\t\t     addr.sin_addr.s_addr == INADDR_BROADCAST ||',
        '\t\t     addr.sin_addr.s_addr == INADDR_ANY)) {',
        '\t\t\terror = EINVAL;',
        '\t\t\tgoto done;',
        '\t\t}',
        '\t\tif (ia == NULL) {',
        '\t\t\tia = in_ifaddr_alloc(Z_WAITOK);',
        '\t\t\tif (ia == NULL) {',
        '\t\t\t\terror = ENOBUFS;',
        '\t\t\t\tgoto done;',
        '\t\t\t}',
        '\t\t\tifnet_lock_exclusive(ifp);',
        '\t\t\tifa = &ia->ia_ifa;',
        '\t\t\tIFA_LOCK(ifa); IFA_ADDREF_LOCKED(ifa); IA_HASH_INIT(ia);',
        '\t\t\tifa->ifa_addr = (struct sockaddr *)&ia->ia_addr;',
        '\t\t\tia->ia_ifp = ifp;',
        '\t\t\tif (!(ifp->if_flags & IFF_LOOPBACK)) in_interfaces++;',
        '\t\t\tif_attach_ifa(ifp, ifa);',
        '\t\t\tif (cmd == SIOCAIFADDR || cmd == SIOCSIFADDR) ifa->ifa_debug |= IFD_NOTREADY;',
        '\t\t\tIFA_UNLOCK(ifa); ifnet_lock_done(ifp);',
        '\t\t\tlck_rw_lock_exclusive(&g_in_ifaddr_rwlock); IFA_ADDREF(ifa);',
        '\t\t\tTAILQ_INSERT_TAIL(&in_ifaddrhead, ia, ia_link);',
        '\t\t\tlck_rw_done(&g_in_ifaddr_rwlock); (void)in_domifattach(ifp); error = 0;',
        '\t\t}',
        '\t\tbreak;',
        '\t}',
        '\tswitch (cmd) {',
        '\tcase SIOCGIFDSTADDR: case SIOCSIFDSTADDR: error = inctl_ifdstaddr(ifp, ia, cmd, ifr); break;',
        '\tcase SIOCGIFBRDADDR: case SIOCSIFBRDADDR: error = inctl_ifbrdaddr(ifp, ia, cmd, ifr); break;',
        '\tcase SIOCGIFNETMASK: case SIOCSIFNETMASK: error = inctl_ifnetmask(ifp, ia, cmd, ifr); break;',
        '\tcase SIOCGIFADDR: case SIOCSIFADDR: case SIOCAIFADDR: case SIOCDIFADDR:',
        '\t\terror = inctl_ifaddr(ifp, ia, cmd, ifr); break;',
        '\tdefault: error = EOPNOTSUPP; break;',
        '\t}',
    ]
    in_lines[1192] = '/*'
    in_lines[1193] = ' * If called directly from within the networking stack (as opposed to via'
    in_lines[1194] = ' * pru_control), the socket parameter may be NULL.'
    in_lines[1195] = ' */'
    for idx, val in enumerate(blk1197, start=1197):
        in_lines[idx - 1] = val

    blk1573 = [
        'done:',
        '\tif (ia != NULL) {',
        '\t\tif (cmd == SIOCDIFADDR) {',
        '\t\t\tIFA_LOCK(&ia->ia_ifa);',
        '\t\t\tia->ia_ifa.ifa_debug &= ~IFD_DETACHING;',
        '\t\t\tif (ia->ia_ifa.ifa_del_waiters > 0) {',
        '\t\t\t\tia->ia_ifa.ifa_del_waiters = 0;',
        '\t\t\t\twakeup(ia->ia_ifa.ifa_del_wc);',
        '\t\t\t}',
        '\t\t\tIFA_UNLOCK(&ia->ia_ifa);',
        '\t\t}',
        '\t\tIFA_REMREF(&ia->ia_ifa);',
        '\t}',
        '\tif (so_unlocked) {',
        '\t\tsocket_lock(so, 0);',
        '\t}',
        '',
        '\treturn error;',
        '}',
    ]
    for idx, val in enumerate(blk1573, start=1573):
        in_lines[idx - 1] = val
    in_path.write_text('\n'.join(in_lines) + '\n', encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitize', action='store_true')
    ap.add_argument('--tsan', action='store_true')
    ap.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    args = ap.parse_args()

    assert PATCH_0080.is_file(), f'missing {PATCH_0080}'
    assert b'\r' not in PATCH_0080.read_bytes(), f'CRLF detected in {PATCH_0080}'

    with tempfile.TemporaryDirectory(prefix='fuxi-net-trace-') as tmp:
        t = Path(tmp)
        build_skeletons(t)
        subprocess.run(['git', 'apply', '--whitespace=nowarn', str(PATCH_0080)], cwd=t, check=True)

        inc = t / 'include'
        for rel in ('pexpert/pexpert.h', 'pexpert/device_tree.h', 'IOKit/IOBSD.h',
                    'netinet/in_arp.h', 'netinet/ip.h', 'netinet/ip6.h', 'netinet6/nd6.h',
                    'sys/protosw.h', 'sys/file.h', 'kern/zalloc.h', 'os/log.h',
                    'net/if.h', 'net/if_types.h', 'net/route.h'):
            hp = inc / rel
            hp.parent.mkdir(parents=True, exist_ok=True)
            hp.write_text('/* host test stub */\n', encoding='utf-8')

        src = t / 'test_net_trace.c'
        src.write_text(HARNESS_SRC, encoding='utf-8')
        binary = t / 'test_net_trace'
        cmd = [args.cc, '-std=c11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
               '-I', str(inc), '-I', str(t), str(src), '-o', str(binary)]
        if args.tsan:
            cmd.extend(['-fsanitize=thread', '-fno-sanitize-recover=all'])
        elif args.sanitize:
            cmd.extend(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
        subprocess.run(cmd, check=True)

        modes = [('--tsan', 'net_trace tsan ok')] if args.tsan else [
            ('--mode-0', 'net_trace mode 0 ok'),
            ('--mode-1-adt', 'net_trace mode 1 ok'),
            ('--mode-1-bootarg', 'net_trace mode 1 ok'),
            ('--mode-2-adt', 'net_trace mode 2 ok'),
            ('--mode-2-bootarg', 'net_trace mode 2 ok'),
            ('--bad-modes', 'net_trace bad modes ok'),
            ('--bad-len', 'net_trace bad len ok'),
        ]
        for flag, expected in modes:
            r = subprocess.run([str(binary), flag], capture_output=True, text=True)
            assert r.returncode == 0, r.stdout + r.stderr
            assert expected in r.stdout, r.stdout

    print('PASS: 0080 sweet-net-trace (fuxi,net-trace-filter 0..2 / sweetnettrace / 10 paths)')


if __name__ == '__main__':
    main()
