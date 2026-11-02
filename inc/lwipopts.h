#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* Minimal lwIP for the poll architecture: outbound UDP services only. */

/* Execution: no OS, single context, static heap */
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define PBUF_POOL_SIZE              16

/* Protocols: UDP carries DHCP, DNS and NTP. ICMP answers ping for diagnostics. */
#define LWIP_IPV4                   1
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_UDP                    1
#define LWIP_TCP                    0
#define LWIP_DHCP                   1
#define LWIP_DNS                    1

/* Required by the CYW43 driver */
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_NETIF_HOSTNAME         1

/* Skip the ARP probe of the offered address: it delays the lease by seconds */
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define LWIP_STATS                  0

#endif
