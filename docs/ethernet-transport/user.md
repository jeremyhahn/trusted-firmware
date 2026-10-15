# Ethernet transport — user guide

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-15
> Ethernet is not in any default image yet. The steps below build one.

## What you need

- An Arduino Nano or Uno (ATmega328P) or an Arduino Mega 2560.
- A WIZnet W5100 shield (Arduino Ethernet Shield Rev3 or compatible) or a W5500
  shield (Arduino Ethernet Shield 2).
- The ATECC608A element for the element tier: signed attestation, the alias
  certificate and enrolment. Without it, build the software tier
  (`CRYPTO=soft`), whose `ATTEST` evidence is an HMAC and which cannot enrol.
- A serial connection for provisioning and for writing the network settings.

The shield uses D10 for the controller's chip select, D4 for its SD card, and
the SPI pins (D11–D13 on the 328P, D50–D53 on the Mega). An Ethernet image
reserves those pins, so GPIO, PWM, the scheduler and `SPI_XFER` refuse them.
Tone is refused in an Ethernet image on both boards: its pin is D11, the SPI
data line, on the 328P and D10, the chip select, on the Mega.

The 328P's element-tier Ethernet image does not yet fit the board's flash budget
and does not build; see the ADR. The Mega's does.

## Build

```
make firmware BOARD=atmega2560 CRYPTO=atecc608a ETH=1
make firmware BOARD=atmega2560 CRYPTO=atecc608a ETH=1 ETH_CHIP=w5500
make firmware BOARD=atmega2560 CRYPTO=soft ETH=1
```

`ETH_CHIP` must match the shield. A mismatch is detected at start and the
Ethernet link stays down; serial still works.

## Configure the network

The device's address and the endpoint it connects to are written once over
serial, using `sdk/go`:

```go
err := dev.WriteNetConfig(ctx, tfw.NetConfig{
    MAC:      mac,
    IP:       netip.MustParseAddr("192.0.2.10"),
    Mask:     netip.MustParseAddr("255.255.255.0"),
    Gateway:  netip.MustParseAddr("192.0.2.1"),
    Endpoint: netip.MustParseAddrPort("192.0.2.20:7020"),
})
```

The device picks the settings up at its next connection attempt, within about a
minute, with no reset. On a network with no router, set `Gateway` to `0.0.0.0`.

The W5100 and W5500 have no built-in MAC address; the device uses whatever the
block holds. An Arduino shield carries its assigned address on a sticker: use
that when you have it. Otherwise use `tfw.DerivedMAC(id)`, a locally
administered unicast address derived from the device's identity, where `id` is
the DeviceID public key (64 bytes, X then Y) on the element tier and the
16-byte serial on the software tier. Writing the sticker address later
replaces the derived one.

## Accept the device

```go
l, err := tfw.Listen(":7020")
dev, err := l.Accept()
caps, err := dev.Caps(ctx)
```

Only `CAPS`, `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` work over Ethernet. Every
other command answers "unknown command"; use serial for those.

## If the device does not connect

- Check that the network block was written: read it back over serial with
  `EEPROM_READ` at `0x0C0`.
- Check `ETH_CHIP` against the shield.
- The device retries on a backoff that grows to a minute. It never stops trying.
