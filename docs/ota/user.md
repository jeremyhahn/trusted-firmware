# OTA — user guide

> **Status: DRAFT** · Issue: #75, #52 · Scope: trusted-firmware · Last verified: 2026-10-16
> Staging an image into the external store of an ATmega2560, as main does it
> today. `tfwctl ota-stage <image>` and `tfwctl ota-status` stage an image and
> read the session ([cli.md](../host-sdk/cli.md)); the steps below use `sdk/go`.

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
`StageImage`.

## At the next reset

Reset the board (`tfwctl reset`, or power). tfw-boot then:

1. reads `MARK` and checks the staged image again: trailer, board id, floor and
   SHA-256;
2. copies it into the secondary slot, checks it there with the full gate,
   signature included, and swaps it into the primary;
3. boots it once on trial.

A refused image is never swapped in, and the previous image keeps running. A
reset during the copy or the swap resumes it at the next boot.

The trial is confirmed only by `CONFIRMED` in the platform block, and nothing
on the wire writes it yet: `OTA_CONFIRM` is a proposal ([api.md](api.md)). So
on main the next reset after a trial swaps the previous image back and marks
the new one `FAILED`; the same staged image is not taken again. The states and
each case are in [architecture.md](architecture.md).

## Not covered here

- The ATmega328P and ATmega32U4: the store copy does not fit their 4 KB boot
  sections, so they have no store row.
- tfw-boot's own serial session (`BOOT_OTA=1`), which writes the secondary
  directly: see [architecture.md](architecture.md).
