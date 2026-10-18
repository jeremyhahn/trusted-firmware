/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Controls for the software W5100 / W5500. See w5x00_model.c for what it models and
 * what it does not. */
#ifndef TFW_W5X00_MODEL_H
#define TFW_W5X00_MODEL_H

#include <stdint.h>

/* What sits on the bus. NONE_LOW and NONE_HIGH are an empty socket with MISO
 * pulled one way or the other: a driver must refuse both. */
#define W5X_MODEL_NONE_LOW   0u
#define W5X_MODEL_W5100      1u
#define W5X_MODEL_W5500      2u
#define W5X_MODEL_NONE_HIGH  3u

/* Powers the bus up with this chip on it, registers at their reset values. */
void     w5x_model_reset(uint8_t chip);

/* The far end of the TCP connection. */
void     w5x_model_peer_accept(void);              /* SYNSENT -> ESTABLISHED */
void     w5x_model_peer_close(void);               /* ESTABLISHED -> CLOSE_WAIT */
void     w5x_model_peer_send(const uint8_t *b, uint16_t n);
uint16_t w5x_model_peer_recv(uint8_t *out, uint16_t cap);  /* what SEND put on the wire */
uint8_t  w5x_model_sends(void);                    /* SEND commands seen */

/* Faults. */
void     w5x_model_set_sr(uint8_t sr);             /* e.g. CLOSED: refused or timed out */
void     w5x_model_cr_stuck(uint8_t on);           /* a command is never taken */
void     w5x_model_cr_stuck_on(uint8_t cr);        /* only command cr is never taken; 0: none */
void     w5x_model_size_unstable(uint8_t on);      /* FSR and RSR never read the same twice */
void     w5x_model_tx_cap(uint16_t cap);           /* FSR reads no more than this */
/* The socket's buffer pointers after the next OPEN, so wrap-around in both the
 * 16-bit pointer and the 2 KB buffer is reachable. */
void     w5x_model_seed_pointers(uint16_t p);
/* What RTR and RCR read after the next reset; 0x07D0 and 0x08 on real parts. */
void     w5x_model_retry_reset(uint16_t rtr, uint8_t rcr);

/* Inspection. */
uint8_t  w5x_model_common(uint16_t off);
uint8_t  w5x_model_sock(uint16_t off);
uint16_t w5x_model_sock16(uint16_t off);
uint32_t w5x_model_writes(void);                   /* register writes the chip took */
uint8_t  w5x_model_pin_output(uint16_t port, uint8_t bit);
uint8_t  w5x_model_pin_high(uint16_t port, uint8_t bit);
uint16_t w5x_model_last_cs_port(void);
uint8_t  w5x_model_last_cs_bit(void);

#endif /* TFW_W5X00_MODEL_H */
