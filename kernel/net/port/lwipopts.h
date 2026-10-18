/* lwIP's options for GLOS (M3 item 3): IPv4 with ARP, ICMP, UDP, TCP, DHCP
 * and (M5) IGMP with the mDNS responder, raw API only, NO_SYS: everything runs in the net thread, so there
 * is no locking. The heap and pools are static, in the kernel's bss. */
#ifndef GLOS_LWIPOPTS_H
#define GLOS_LWIPOPTS_H

#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_TIMERS                 1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_ACD                    0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_IGMP                   1           /* for mDNS's group, 224.0.0.251 */
#define LWIP_MDNS_RESPONDER         1           /* NAME.local (M5): third_party/lwip/src/apps/mdns */
#define LWIP_NUM_NETIF_CLIENT_DATA  1           /* (the responder's, on the interface) */
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1        /* (it follows the address DHCP gives) */
#define MDNS_MAX_SERVICES           1
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 12)    /* (and its timers, SNTP's) */
#define LWIP_DNS                    1           /* (M10) the names DHCP's servers answer for */
#define DNS_TABLE_SIZE              8
#define DNS_MAX_NAME_LENGTH         128
#define DNS_MAX_SERVERS             2

/* SNTP (M10): lwIP's client (third_party/lwip/src/apps/sntp), the time handed to kernel/core/clock.c. The interval
   is [time] interval's, an hour at least (RFC 4330's 15 seconds kept); the port 123, or the server's as GLOS.CFG
   gives it (a test's on the host). */
extern unsigned short clock_sntp_port;
unsigned int clock_sntp_interval(void);
void clock_sntp(unsigned int sec, unsigned int us);
void clock_get_us(unsigned int *sec, unsigned int *us);
#define SNTP_SERVER_DNS             1
#define SNTP_MAX_SERVERS            1
#define SNTP_CHECK_RESPONSE         2
#define SNTP_COMP_ROUNDTRIP         1
#define SNTP_STARTUP_DELAY          0
#define SNTP_SUPPRESS_DELAY_CHECK   1
#define SNTP_UPDATE_DELAY           clock_sntp_interval()
#define SNTP_RETRY_TIMEOUT          15000
#define SNTP_PORT                   clock_sntp_port
#define SNTP_SET_SYSTEM_TIME_US(sec, us) clock_sntp((unsigned int)(sec), (unsigned int)(us))
#define SNTP_GET_SYSTEM_TIME(sec, us) do { unsigned int s_, u_; clock_get_us(&s_, &u_); (sec) = s_; (us) = u_; } while (0)

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_STATS                  0

#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (128 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            64
#define PBUF_POOL_SIZE              48

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define LWIP_TCP_KEEPALIVE          1

#endif
