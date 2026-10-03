/* lwIP's options for GLOS (M3 item 3): IPv4 with ARP, ICMP, UDP, TCP and
 * DHCP, raw API only, NO_SYS: everything runs in the net thread, so there
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
#define LWIP_IGMP                   0
#define LWIP_DNS                    0

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
