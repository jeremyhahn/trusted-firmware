/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * lwIP's options for the STM32H5 ETH_MAC=1 image (#581): NO_SYS raw API from
 * the main loop, IPv4 with ARP, ICMP, UDP for DHCP and TCP for the tfw-wire
 * link of the next slice. No sockets, netconn, IPv6, DNS or IGMP. The heap
 * and pools fit in a few tens of KiB of the H573's 640 KiB SRAM.
 */
#ifndef TFW_H5_LWIPOPTS_H
#define TFW_H5_LWIPOPTS_H

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
#define LWIP_ACD                    0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    0
#define LWIP_IGMP                   0
#define IP_REASSEMBLY               0
#define IP_FRAG                     0

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_HOSTNAME         0
#define LWIP_SINGLE_NETIF           1

/* lwIP carves struct pbuf (which holds pointers) out of its pools at
 * MEM_ALIGNMENT, so it must be at least the pointer alignment: 4 on the
 * Cortex-M33 target, 8 when the host unit tests build this file on a
 * 64-bit machine (UBSan rejects the misaligned pbuf otherwise). */
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ > 4
#define MEM_ALIGNMENT               8
#else
#define MEM_ALIGNMENT               4
#endif
#define MEM_SIZE                    (8 * 1024)
#define MEMP_NUM_PBUF               8
#define MEMP_NUM_UDP_PCB            2
#define MEMP_NUM_TCP_PCB            2
#define MEMP_NUM_TCP_PCB_LISTEN     1
#define MEMP_NUM_TCP_SEG            8
#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1536

#define TCP_MSS                     536
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            8

#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif /* TFW_H5_LWIPOPTS_H */
