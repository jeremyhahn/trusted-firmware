# Device capabilities: API

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

One transport exists: the framed serial protocol described in full in
`../wire-protocol/architecture.md` (frame layout, CRC, resync, versioning). This
page covers only the command/payload bodies this feature set puts inside that
frame. `STATUS` in a response is one byte from `include/tfw/status.h`; see
`engineering.md` for which codes each command can return.

All multi-byte fields are little-endian. `tfw_pin_t` is one byte, the number
printed on the board silkscreen — resolved to hardware by the board
descriptor, never sent as a register address.

## Discovery: `CAPS` (`0x00`)

Not owned by this feature (system-wide, `src/caps/sys.c`) but the mechanism
every command below assumes: `CAPS` with no payload returns the feature
bitmap; `CAPS` with a one-byte pin index returns that pin's flags, port/bit,
ADC channel and PWM state. Full layout: `../wire-protocol/api.md#discovery-caps-0x00`.

## GPIO

| command | id | min/max payload |
|---|---|---|
| `PIN_MODE` | `0x10` | 2 / 2 |
| `DIGITAL_READ` | `0x11` | 1 / 1 |
| `DIGITAL_WRITE` | `0x12` | 2 / 2 |

```
PIN_MODE      req  pin:u8  mode:u8(0=in,1=out,2=pullup)      resp  (none)
DIGITAL_READ  req  pin:u8                                    resp  level:u8
DIGITAL_WRITE req  pin:u8  level:u8(0|1)                      resp  (none)
```

## Pin scheduler

| command | id | min/max payload |
|---|---|---|
| `TIMER_SET` | `0x14` | 6 / 6 |
| `TIMER_CANCEL` | `0x15` | 2 / 2 |
| `TIMER_QUERY` | `0x16` | 1 / 1 |

```
TIMER_SET     req  pin:u8  level:u8(0|1)  ms:u32              resp  slot:u8
TIMER_CANCEL  req  pin:u8  apply_final:u8(0|1)                resp  (none)
TIMER_QUERY   req  pin:u8                                     resp  active:u8  held_level:u8  remaining_ms:u32
```

`ms` must be in `[1, 0x7FFFFFFF]`. `remaining_ms` is clamped to zero for a
deadline that has passed but not yet been swept.

## Analog

| command | id | min/max payload |
|---|---|---|
| `ANALOG_READ` | `0x20` | 1 / 1 |
| `ADC_REF` | `0x21` | 1 / 1 |

```
ANALOG_READ  req  pin:u8                                      resp  raw:u16
ADC_REF      req  ref:u8(0=Vcc,1=external,2=internal)         resp  (none)
```

`raw` is the converter count, unscaled — see `architecture.md` for why.

## PWM and tone

| command | id | min/max payload |
|---|---|---|
| `PWM_WRITE` | `0x22` | 2 / 2 |
| `TONE` | `0x23` | 3 / 3 |
| `NO_TONE` | `0x24` | 0 / 1 |

```
PWM_WRITE  req  pin:u8  duty:u8(0..255)                       resp  (none)
TONE       req  pin:u8  hz:u16                                resp  (none)
NO_TONE    req  (pin ignored if present)                      resp  (none)
```

`duty=0` disconnects the pin from its compare unit rather than driving it
low. `TONE` is refused on every pin except the board's single `TFW_TONE_PIN`.

## Pulse

| command | id | min/max payload |
|---|---|---|
| `PULSE_IN` | `0x30` | 6 / 6 |
| `PULSE_CAPTURE` | `0x34` | 7 / 7 |

```
PULSE_IN       req  pin:u8  level:u8(0|1)  timeout_us:u32     resp  width_us:u32
PULSE_CAPTURE  req  pin:u8  start_level:u8(0|1)  max_edges:u8  timeout_us:u32
               resp  n:u8  interval_us:u16 * n
```

`timeout_us` is bounded by `TFW_PULSE_MAX_US` (50000 by default); `max_edges`
by `TFW_CAPTURE_MAX = (TFW_MAX_PAYLOAD - 1) / 2`, the most intervals one reply
can carry. Intervals are raw edge-to-edge microsecond counts; the host
decodes the sensor.

**`0x31`–`0x33`** (`PULSE_COUNT_START`/`READ`/`STOP`) are reserved in
`include/tfw/protocol.h` but have no entry in `src/core/commands.def`, no
capability handler, and no SDK method. A request against them returns
`TFW_ERR_UNKNOWN_CMD`, indistinguishable from an id that was never allocated.

## EEPROM

| command | id | min/max payload |
|---|---|---|
| `EEPROM_READ` | `0x40` | 3 / 3 |
| `EEPROM_WRITE` | `0x41` | 3 / `TFW_MAX_PAYLOAD` |

```
EEPROM_READ   req  addr:u16  len:u8                            resp  data:blob(len)
EEPROM_WRITE  req  addr:u16  data:blob                         resp  (none)
```

`len`/`len(data)` is bounded by `TFW_EEPROM_CHUNK` (8). `addr` below `0x080`
is refused on write (`TFW_ERR_PROTECTED_REGION`) and permitted on read.

## Buses

| command | id | min/max payload |
|---|---|---|
| `I2C_XFER` | `0x50` | 2 / `TFW_MAX_PAYLOAD` |
| `SPI_XFER` | `0x51` | 2 / `TFW_MAX_PAYLOAD` |
| `ONEWIRE_RESET` | `0x52` | 1 / 1 |
| `ONEWIRE_WRITE` | `0x53` | 2 / `TFW_MAX_PAYLOAD` |
| `ONEWIRE_READ` | `0x54` | 2 / 2 |
| `I2C_CONFIG` | `0x55` | 4 / 4 |
| `SPI_CONFIG` | `0x56` | 3 / 3 |

```
I2C_XFER        req  addr7:u8  rlen:u8  wr:blob                resp  rd:blob(rlen)
SPI_XFER        req  cs_pin:u8  wr:blob                        resp  rd:blob(len(wr))
ONEWIRE_RESET   req  pin:u8                                    resp  presence:u8
ONEWIRE_WRITE   req  pin:u8  wr:blob                            resp  (none)
ONEWIRE_READ    req  pin:u8  n:u8                               resp  data:blob(n)
I2C_CONFIG      req  hz:u32                                    resp  (none)
SPI_CONFIG      req  mode:u8(0-3)  clkdiv:u8(0-3)  lsb_first:u8(0|1)  resp  (none)
```

`addr7 == TFW_I2C_RESERVED_ADDR` (`0x60` by default) is refused unconditionally.
`cs_pin` flagged `TFW_PIN_RESERVED`/`TFW_PIN_IN_USE` (a TPM's chip-select, or the
UART pins) is refused the same way. SPI is full duplex: the reply is always
the same length as the write. Transfer length on all three buses is bounded
by `TFW_MAX_PAYLOAD - 1`.

`I2C_CONFIG` and `SPI_CONFIG` reconfigure the generic bus's clock (I2C) or
mode/clock-divider/bit-order (SPI). Neither is required before `I2C_XFER` or
`SPI_XFER` work: `main.c` calls `tfw_bus_init()` at boot, which applies
`TFW_I2C_DEFAULT_HZ` (100 kHz) and `TFW_SPI_DEFAULT_MODE`/`_CLKDIV`/`_LSB_FIRST`
(mode 0, slowest divider, MSB-first) before the host ever sends either
command. `hz` outside `TFW_I2C_HZ_MIN..TFW_I2C_HZ_MAX` (1 kHz..400 kHz), or `mode`
/`clkdiv` above 3, or `lsb_first` above 1, is `TFW_ERR_RANGE`.

On a build whose crypto tier is the secure element (`CryptoTier ==
CryptoSecureElement` in `CAPS`), `I2C_CONFIG` refuses EVERY request with
`TFW_ERR_PROTECTED_REGION`, checked before the range above -- the element's
wake pulse needs this same TWI peripheral at or below 100 kHz, and a rate
accepted here would be silently undone the next time any crypto operation
ran. `SPI_CONFIG` carries no such restriction on any tier.
