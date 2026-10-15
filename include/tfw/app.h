/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The application interface: what a trusted-firmware-<application> repository
 * defines so its code is linked into the firmware image, and the little the
 * firmware offers it back. The contract -- when each hook runs, what it may do,
 * its budget, what a failure means -- is docs/application-sdk/engineering.md.
 *
 * Every hook is an ordinary external symbol, resolved by the linker. There is
 * no table of function pointers and no weak default: an application that leaves
 * a hook out fails to link and names the hook, where a weak default would link
 * the misspelt one silently and never run it. A build with no application links
 * src/core/app_runner_none.c in place of the runner, so nothing references
 * these symbols and none of them is defined.
 *
 * An application includes the headers of include/tfw/ and nothing else of this repository.
 */
#ifndef TFW_APP_H
#define TFW_APP_H

#include <stddef.h>
#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"
#include "tfw/config.h"
#include "tfw/progmem.h"
#include "tfw/boot.h"

/* "TFWA" in memory order, so the descriptor can be found in an image by eye. */
#define TFW_APP_MAGIC        0x41574654uL

/* The version of this interface: the hook set, their signatures and the
 * descriptor layout. Raised by any change an application built against the old
 * header would misbehave under; the runner refuses a descriptor that differs. */
#define TFW_APP_ABI          1u

#define TFW_APP_NAME_LEN     16u
#define TFW_APP_PROFILE_LEN  32u

/* The update path an application relies on. Compared with TFW_OTA_SLOTS. */
#define TFW_APP_OTA_NONE     0u
#define TFW_APP_OTA_SINGLE   1u
#define TFW_APP_OTA_AB       2u

/* A required capability, by its CAPS Features bit (TFW_CAPBIT_* in config.h). */
#define TFW_APP_CAP(bit)     ((uint32_t)1u << (bit))

/* The application's run-time configuration starts at the first EEPROM byte
 * above the platform block, which EEPROM_WRITE already refuses to touch. */
#define TFW_APP_CONFIG_BASE  TFW_BOOT_EE_LEN

typedef struct {
    uint32_t magic;          /* TFW_APP_MAGIC */
    uint8_t  abi;            /* TFW_APP_ABI the application was built against */
    uint8_t  ota_slots;      /* TFW_APP_OTA_* */
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t version_patch;
    uint16_t config_len;     /* EEPROM bytes owned, from TFW_APP_CONFIG_BASE */
    uint32_t required_caps;  /* TFW_APP_CAP() bits the image must have */
    char     name[TFW_APP_NAME_LEN];       /* 1..15 characters and a NUL */
    char     profile[TFW_APP_PROFILE_LEN]; /* device profile; "" is the default */
} tfw_app_descriptor_t;

/* The layout is read field by field out of flash and will go on the wire as
 * it stands, so it is pinned, not left to the compiler. Every field is
 * naturally aligned, which is what makes the two architectures agree. */
_Static_assert(sizeof(tfw_app_descriptor_t) == 64u, "tfw_app_descriptor_t is 64 bytes");
_Static_assert(offsetof(tfw_app_descriptor_t, abi) == 4u, "descriptor abi at 4");
_Static_assert(offsetof(tfw_app_descriptor_t, version_patch) == 8u, "descriptor version_patch at 8");
_Static_assert(offsetof(tfw_app_descriptor_t, config_len) == 10u, "descriptor config_len at 10");
_Static_assert(offsetof(tfw_app_descriptor_t, required_caps) == 12u, "descriptor required_caps at 12");
_Static_assert(offsetof(tfw_app_descriptor_t, name) == 16u, "descriptor name at 16");
_Static_assert(offsetof(tfw_app_descriptor_t, profile) == 32u, "descriptor profile at 32");

/* ------------------------------------------------ defined by the application */

/* In flash: define it `const tfw_app_descriptor_t tfw_app_descriptor
 * TFW_PROGMEM = { ... };`. Read only through tfw_pgm_*. */
extern const tfw_app_descriptor_t tfw_app_descriptor;

/* Once, at the end of startup, before interrupts are enabled: the tick does not
 * advance and the link is not up. TFW_OK starts the application; anything else
 * leaves the device firmware-only until the next reset, with nothing of the
 * application called again. Budget 20 ms. */
tfw_status_t tfw_app_init(void);

/* Every superloop pass while running. now_ms is tfw_hal_millis() at the call.
 * Return within TFW_APP_RUN_BUDGET_MS; never wait, never touch the UART. */
void tfw_app_run(tfw_ms_t now_ms);

/* Before a commanded reset, once its reply has drained. Best effort: not called
 * on power loss, brown-out or a watchdog reset. Budget 10 ms with the next. */
void tfw_app_shutdown(void);

/* Immediately before tfw_app_shutdown when the reset is into the bootloader for
 * an update. A notification: it cannot stop the update. */
void tfw_app_before_update(void);

/* On the first boot of a newly installed image, after tfw_app_init returned
 * TFW_OK. TFW_OK confirms the image; anything else leaves it unconfirmed, so
 * the bootloader rolls back. Called by the ota work, not yet by this firmware. */
tfw_status_t tfw_app_after_update(void);

/* ----------------------------------------------------- defined by the firmware */

/* The application's EEPROM region, [0, config_len). TFW_ERR_RANGE outside it,
 * otherwise the HAL's EEPROM status. An AVR EEPROM byte takes about 3.4 ms to
 * write, so a write of more than a byte belongs in init, not in run. */
tfw_status_t tfw_app_config_read(uint16_t off, uint8_t *buf, uint8_t len);
tfw_status_t tfw_app_config_write(uint16_t off, const uint8_t *buf, uint8_t len);

/* At file scope in the application: a compile error, rather than a device that
 * boots firmware-only, when this image lacks a capability the application needs
 * or the board has fewer update slots than it relies on. The runner checks the
 * same at boot for an application that does not use this. */
#define TFW_APP_STATIC_CHECK(caps, ota) \
    _Static_assert(((uint32_t)(caps) & ~(uint32_t)TFW_FEATURE_BITMAP) == 0u, \
                   "this image lacks a capability the application requires"); \
    _Static_assert((ota) <= TFW_OTA_SLOTS, \
                   "this board has fewer update slots than the application relies on")

#endif /* TFW_APP_H */
