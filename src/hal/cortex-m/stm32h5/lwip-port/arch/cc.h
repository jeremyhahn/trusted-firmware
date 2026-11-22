/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * lwIP's compiler and platform port for the STM32H5 ETH_MAC=1 image (#581):
 * GCC on newlib-nano, little-endian, no diagnostic output (the console is the
 * tfw-wire UART), and LWIP_RAND from the netif glue.
 */
#ifndef TFW_H5_LWIP_CC_H
#define TFW_H5_LWIP_CC_H

#include <stdint.h>

#define LWIP_PLATFORM_DIAG(x)    do { } while (0)
#define LWIP_PLATFORM_ASSERT(x)  do { } while (0)
#define LWIP_NO_INTTYPES_H       1
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "u"

/* DHCP xids and local ports: xorshift32 seeded from the hardware RNG at
 * bring-up (hal_eth_netif.c). TCP ISNs do not use it (lwipopts.h). */
uint32_t tfw_h5_eth_rand(void);
#define LWIP_RAND()              tfw_h5_eth_rand()

#endif /* TFW_H5_LWIP_CC_H */
