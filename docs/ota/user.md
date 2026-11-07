# OTA — user guide

> **Status: DRAFT** · Issue: #75, #52 · Scope: trusted-firmware · Last verified: 2026-10-16
> Staging an image into the external store of an ATmega2560, as main does it
> today. `tfwctl ota-stage <image>` and `tfwctl ota-status` stage an image and
> read the session, and `tfwctl ota-activate [now]` takes it
> ([cli.md](../wire-protocol/cli.md)); the steps below also use `sdk/go`.

## What you need

- An Arduino Mega 2560 with an SPI NOR part on D8 (`TFW_OTA_STORE_CS_PIN`, a
  proposal: the ADR's open question 2). The store holds the image in its
  `STAGE` area, 65 536 bytes from address `0x000000`, and the `MARK` record at
  `0x011000`.
- The ATECC608C for the element tier.
- tfw-boot built on the store row and written over ISP:

  ```sh
  make bootloader BOARD=atmega2560 BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a \
       BOOT_SIG=atecc608a BOOT_OTA=0 BOOT_STORE=1
  make boot-flash BOARD=atmega2560 ...   # the same settings
  ```

- An application image with the stager: `ETH=1` links it, and `OTA_STAGE=1`
  does on a serial-only image ([engineering.md](engineering.md)).
- The new image: trailer v1 with this board's id (`0x02`), signed for the
  element tier, padded to whole 256-byte pages with its trailer last. This
  project does not sign images; the key is go-xkms's.

## Seal the image

The build writes the trailer, and go-xkms signs it. `IMAGE_FLAGS` defaults to
`0x00`; `0x01` marks a development image.

```sh
make image-tbs BOARD=atmega2560 CRYPTO=<tier> IMAGE_VERSION=<n>
# build/release/atmega2560-<tier>.tbs: the 48-byte signed span, this
# board's id in byte 6. go-xkms signs it with ES256 and returns the raw
# 64-byte r||s signature.
make image-seal BOARD=atmega2560 CRYPTO=<tier> IMAGE_SIG=<sig.bin>
# build/release/atmega2560-<tier>.stream  what ota-stage sends
# build/release/atmega2560-<tier>.region  the application region, for
#                                         tfw-boot's serial session
```

Both refuse what tfw-boot would refuse: an empty image or one that reaches the
trailer, version 0, a flag trailer v1 does not define, a board with no
tfw-boot row, and a span for another image. The signature is not checked
until the device checks it. `IMAGE_VERSION` must not be below the board's
floor, or tfw-boot refuses the image.

## Stage the image

From a shell, over serial ([cli.md](../host-sdk/cli.md)):

```sh
tfwctl <port> ota-stage <image>    # OTA_BEGIN, OTA_DATA…, OTA_END, then OTA_STATUS
complete, <n> bytes received

tfwctl <port> ota-status           # the session's state, printed the same way
```

The state is `idle`, `receiving`, `complete` or `failed`; `-json` prints
a JSON object with `received` and `state` instead. tfwctl does not parse the
image: the device gates it. A refusal, a transport error or an unreadable file
prints `tfwctl: <error>` on stderr and exits 1; a wrong argument count exits 1
with the verb's usage.

`ota-stage` stages through `Upload` and prints its progress on stderr,
`ota-stage: <sent>/<total> bytes`. When an `OTA_DATA` fails, it asks
`OTA_STATUS` where the session stands and, while the device is still
`receiving`, goes on from the device's count, so no accepted byte is sent
twice. A new `ota-stage` run sends `OTA_BEGIN` and starts over from byte 0.

From a program, over serial or TCP, use `sdk/go`. Over serial, wrap the port in
any `io.ReadWriter`; over TCP, accept the device's connection on the endpoint
port its network block names
([Ethernet user guide](../ethernet-transport/user.md)).

```go
// serial
dev := tfw.New(port)

// or TCP: the device dials in
l, err := tfw.Listen(":<endpoint port>")
dev, err := l.Accept() // *tfw.TCPDevice, which embeds *tfw.Device

err = dev.StageImage(ctx, image)
state, received, err := dev.OTAStatus(ctx) // OTAComplete, len(image)
```

`StageImage` returns the device's status when it refuses the image; the codes
are in [api.md](api.md). A refused or interrupted transfer leaves no `MARK`, and
the running image is untouched. Start again with `tfwctl ota-stage` or
`StageImage`; `Upload` is `StageImage` that resumes within the session.

## Activate the image

A staged image is not taken by itself: `OTA_END` leaves it verified in RAM and
writes no `MARK`, so a reset after staging alone boots the running image again.
The operator, or the control plane after its checks, activates it:

```sh
tfwctl <port> ota-activate         # OTA_ACTIVATE mode 0: taken at the next reset
activated; taken at the next reset

tfwctl <port> ota-activate now     # mode 1: the device resets after its reply
activated; the device resets now
```

`-json` prints `{"activated": true, "reset_now": <bool>}`. The device
re-reads and re-verifies the whole staged image, then writes `MARK`. It
answers `TFW_ERR_BAD_STATE` (19) when no verified image is staged, when a trial
is running or its confirm is not yet settled, or when the image fails the
re-check; `TFW_ERR_RANGE` (11) for a mode other than 0 or 1; and, on the
element tier, the auth gate's status outside an operator session. tfwctl prints
`tfwctl: <error>` on stderr and exits 1, as it does for a wrong argument
(`usage: ota-activate [now]`).

The staged state lives in RAM. A board that resets when its port is opened
(the Mega 2560 over USB serial) has lost it by the time a separate
`ota-activate` connects, and answers `TFW_ERR_BAD_STATE`. Stage and activate on
one connection instead:

```sh
tfwctl <port> ota-stage --activate <image>       # stage, activate, then print OTA_STATUS
tfwctl <port> ota-stage --activate=now <image>   # stage, activate, reset now
activated; the device resets now
```

With `--activate=now` tfwctl sends nothing after `OTA_ACTIVATE`'s reply: the
device is resetting, so it does not read `OTA_STATUS`, and `-json` prints
`{"activated": true, "reset_now": true}`. A refusal at either step exits 1.

A board with one slot, the ATmega328P or ATmega32U4 with a stager, answers
`OTA_ACTIVATE` the same way, with the same re-check, the same `MARK` and the
same refusals. At the reset its tfw-boot copies the image into its one internal
slot, with the previous image backed up in the store.

## At the next reset

After `ota-activate`, reset the board (`tfwctl reset`, or power); after
`ota-activate now` it resets itself. tfw-boot then:

1. reads `MARK` and checks the staged image again: trailer, board id, floor and
   SHA-256;
2. copies it into the secondary slot, checks it there with the full gate,
   signature included, and swaps it into the primary;
3. boots it once on trial.

A refused image is never swapped in, and the previous image keeps running. A
reset during the copy or the swap resumes it at the next boot.

The trial is confirmed only by `OTA_CONFIRM` (0x6E, [api.md](api.md)), which
the control plane sends to the running new image after attestation; tfwctl has
no verb for it, so a host sends it through `sdk/go`:
`(*Device).OTAConfirm(ctx context.Context) error` (`sdk/go/ota.go`). Without
it the next reset after a trial swaps the previous image back and marks the
new one `FAILED`; the same staged image is not taken again. The states and
each case are in [architecture.md](architecture.md).

## Not covered here

- The ATmega328P and ATmega32U4 in detail: they have a store row with one slot
  and the previous image kept as `PREV` in the store, copied back at a fallback
  (`mk/boot.mk`, #52); "Activate the image" above covers what differs.
- tfw-boot's own serial session (`BOOT_OTA=1`), which writes the secondary
  directly: see [architecture.md](architecture.md).
