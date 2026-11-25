# Application SDK — user guide

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-21
> Conflicts with this document are defects. Fix them here first.

For someone writing a `trusted-firmware-<application>` repository. The
contract behind each step is in [engineering.md](engineering.md); the
declarations are in [api.md](api.md).

## Start an application

Lay the repository out like this. Only `app.mk` has a fixed name.

```
trusted-firmware-heartbeat/
  app.mk
  heartbeat.c
```

`app.mk`:

```make
APP_NAME := heartbeat
APP_SRCS := heartbeat.c
```

`heartbeat.c` defines the descriptor and all six hooks. A hook you have no
use for is an empty function, or for `tfw_app_evidence` one that returns
`TFW_ERR_UNSUPPORTED`; leaving one out is a link error. The heartbeat drives
pin 13, the LED, on a board that has one, and pin 0 on a board with fewer pins.

```c
#include "tfw/app.h"
#include "tfw/board.h"
#include "tfw/hal.h"

#define HEARTBEAT_CAPS (TFW_APP_CAP(TFW_CAPBIT_GPIO))
TFW_APP_STATIC_CHECK(HEARTBEAT_CAPS, TFW_APP_OTA_NONE);

const tfw_app_descriptor_t tfw_app_descriptor TFW_PROGMEM = {
    .magic = TFW_APP_MAGIC, .abi = TFW_APP_ABI, .ota_slots = TFW_APP_OTA_NONE,
    .version_major = 1u, .required_caps = HEARTBEAT_CAPS, .name = "heartbeat",
};

/* Pin 13 is the LED on the Arduino boards; a board with fewer pins (esp32-qemu,
 * mps2-an505) has no pin 13, so the heartbeat takes its first pin instead. */
#if TFW_PIN_COUNT > 13u
#define HEARTBEAT_PIN 13u
#else
#define HEARTBEAT_PIN 0u
#endif
_Static_assert(TFW_PIN_COUNT == 0u || HEARTBEAT_PIN < TFW_PIN_COUNT,
               "the heartbeat pin is not a pin of this board");

static tfw_pinref_t led;
static tfw_ms_t     next;
static uint8_t      level;

tfw_status_t tfw_app_init(void)
{
    tfw_status_t st = tfw_board_pin(HEARTBEAT_PIN, &led);
    if (st != TFW_OK) {
        return st;
    }
    return tfw_hal_gpio_mode(&led, TFW_PIN_OUTPUT);
}

void tfw_app_run(tfw_ms_t now_ms)
{
    if ((int32_t)(now_ms - next) < 0) {
        return;                      /* not yet: return, never wait */
    }
    next  = now_ms + 500u;
    level = (uint8_t)!level;
    (void)tfw_hal_gpio_write(&led, level);
}

void tfw_app_shutdown(void)        { (void)tfw_hal_gpio_write(&led, 0u); }
void tfw_app_before_update(void)   {}
tfw_status_t tfw_app_after_update(void) { return TFW_OK; }
tfw_status_t tfw_app_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;      /* none: the claim carries the firmware hash */
}
```

A complete version of this is `test/app/example/` in this repository, and the
test suite builds it from a directory outside the tree, so it stays correct.

## Write a `run` that keeps the device responsive

- Return within 2 ms. Keep state in statics and pick up where you left off on
  the next call.
- Compare times with a signed difference, as above, so the 49-day wrap of the
  millisecond tick does not stop you.
- Never wait on `tfw_hal_millis()` and never call the UART. The firmware is
  serving the host between your calls.
- Write configuration one byte per call. An AVR EEPROM byte takes about 3.4 ms.

A slow `run` is not stopped. It is counted, and the device drops host bytes
while it runs.

## Put your evidence in the device's attestation

`tfw_app_evidence` hands the firmware 32 bytes for the measurements claim
(273) of the alias certificate, signed under the device's DeviceID. A verifier
matches them against the reference registered for your application, so make
them something that reference can name: a SHA-256 of your configuration, say,
or of the state you report. Keep the value current in `run` and only copy it
out here; the call happens inside a host command and has 10 ms.

Return `TFW_OK` only when `out` holds the bytes. Anything else, and the claim
carries the firmware hash instead, never zeros and never half of what you
wrote. The test example returns `0xE0..0xFF`; read them back with
`tfwctl -json <port> attest-cert <nonce>` as `app_evidence`.

## Build it

Build against a released trusted-firmware tag, not a branch. The descriptor's
`abi` is checked when the device boots, so an application built against one
interface version does not start on another. The board is a build argument,
never a fork: one application repository builds for every board the framework
supports.

From the trusted-firmware checkout:

```sh
make tfwsim APP_DIR=../trusted-firmware-heartbeat
make firmware APP_DIR=../trusted-firmware-heartbeat BOARD=atmega2560
```

The first builds `build/host/tfwsim-app-heartbeat`, the firmware with your
application against the mock HAL. It speaks the wire protocol on stdin and
stdout, so the Go SDK and its tests talk to it as to a device. The second
builds the Mega's image under `build/atmega2560/`, in a directory named for its
tier and options and ending `-app-heartbeat`, and fails if the image is over the
board's budget. The ATmega328P has no soft tier since owner ruling 2026-10-17, and its element image has no room for an application
([engineering](engineering.md)).

Include only `tfw/*.h`. The build gives your files no path into the
firmware's `src/`, so an internal header fails to compile. That is on purpose:
the public headers are the interface that stays stable across firmware
versions.

## Configure it at run time

Declare how many bytes you need in `config_len`. Your region starts at
`TFW_APP_CONFIG_BASE` (`TFW_NETCFG_ADDR + TFW_NETCFG_LEN`, byte 0x160 = 352 of
EEPROM, on every image; the bootloader's block, on the ATmega2560 its swap
journal, and the 32-byte network block at `[0x140, 0x160)` lie below it).
Read it in `init`:

```c
uint8_t cfg[4];
if (tfw_app_config_read(0u, cfg, sizeof cfg) != TFW_OK || cfg[0] != 1u) {
    /* erased (0xFF) or an old layout: use defaults */
}
```

Both calls take an offset into your region, `[0, config_len)`, and return
`TFW_ERR_RANGE` outside it. The application writes the region with
`tfw_app_config_write(off, buf, len)`: in `run`, one byte per call, as above.

A host writes the same bytes with the Go SDK's `Device.EEPROMWrite`
(`sdk/go/commands.go`) at address `0x160 + offset` (`tfw.AppConfigBase +
offset`); the device refuses a write below 0x140. Your region never overlaps
the network block `[0x140, 0x160)`, which is reserved on every image, Ethernet
or not; a host write into that block needs an authenticated operator session
where the image has one (`src/caps/storage.c`). Put a layout version in the
first byte, and validate everything you read: any host with wire access can
write the region. Do not store secrets there.

An out-of-tree application whose config was stored at 0x140, before #640 moved
the region to 0x160, finds its region erased or holding other bytes after the
update: re-provision it at `0x160 + offset`.

## Update it

Declare the update path you rely on in `ota_slots`. `TFW_APP_OTA_SINGLE` is
what AVR boards have today: tfw-boot replaces the image in place.
`TFW_APP_OTA_AB` needs a board with two slots; on any other board your
application refuses to start rather than run without its rollback.

Before the device resets into the bootloader, the firmware calls
`tfw_app_before_update` and then `tfw_app_shutdown`: on `RESET` mode 1, on
`OTA_ACTIVATE` mode 1, whose reset installs the staged image, and on a
`RESET` mode 0 after `OTA_ACTIVATE` mode 0, which leaves the install to that
reset. Save anything the next version needs. You cannot refuse the update. A
`RESET` mode 0 with no image marked calls only `tfw_app_shutdown`.

After an update, `tfw_app_after_update` is called once on the new image's
trial boot, straight after `init` succeeds. Return `TFW_OK` when the new image
works: the trial runs on, and the control plane confirms the image with
`OTA_CONFIRM` after it attests the device. Anything else ends the trial at
once: the device resets and `tfw-boot` rolls back to the previous image. It is
not called on any other boot.

## When `init` fails

Return the status that says why, with your outputs back in their safe state.
The device carries on as firmware only: it answers the host, attests and
accepts a fixed image. Nothing of yours is called again until the next reset.
The same happens without `init` being called when the image cannot run your
application: a missing capability, too few update slots, or a configuration
region larger than the board's EEPROM.

On the trial boot of a newly installed image, a failed `init`, or an image
that cannot run your application, ends the trial at once: the firmware resets
and `tfw-boot` rolls back to the previous image. `tfw_app_after_update` is not
asked. Recovery is another update.

## Check it on the simulator

`build/host/tfwsim-app-heartbeat` keeps the mock HAL's clock and pins. A
`RESET` there stops your application and does not restart it, because the
simulator does not reboot. Test a full update cycle on a board.
