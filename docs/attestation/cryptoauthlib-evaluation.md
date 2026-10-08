# CryptoAuthLib evaluation — decision record

Scope: whether `trusted-firmware` links Microchip's CryptoAuthLib to drive the
ATECC608A, or writes a minimal purpose-built driver.

**Decision: a minimal purpose-built driver.** Blocked on licence; would not fit
regardless. Both are recorded below with the evidence, because the reason to
write something Microchip already wrote had better be a number and not a
preference.

- Evaluated: CryptoAuthLib v3.8.0, commit `d49c7d578efb09d4a498d5ab86839d4c17812f15` (2026-05-14)
- Evaluated on: 2026-09-22, avr-gcc 14.2.0, `-mmcu=atmega328p -Os`
- Reproduce: `git clone --depth 1 https://github.com/MicrochipTech/cryptoauthlib`,
  generate `atca_config.h` from `lib/atca_config.h.in`, compile the `calib_*`
  command layer and `atca_iface.c`, `avr-size -t`

## Finding 1 — the licence is incompatible with AGPL-3.0-or-later. This alone decides it.

`license.txt`, first paragraph:

> Subject to your compliance with these terms, you may use the Microchip
> Software and any derivatives **exclusively with Microchip products**.

That is a field-of-use restriction, and it survives into derivatives. This
project is AGPL-3.0-or-later, and GPLv3 §7 permits no further restrictions on
the rights it grants: a recipient of our firmware must be free to use, modify
and redistribute it for **any** purpose, including on a part that is not
Microchip's. Linking CryptoAuthLib into a distributed AGPL work would hand every
downstream recipient a licence we have no right to narrow.

Redistribution in source or binary is otherwise permitted, and the terms are not
onerous — the incompatibility is specifically the exclusivity clause, not the
attribution or warranty language.

This is not a size problem that a smaller subset would solve, and not something
a build flag can switch off. It ends the question on its own.

## Finding 2 — it would not fit, independently

Sixteen of the seventeen core files compile for AVR. Measured `.text`:

| | bytes |
|---|---|
| **CryptoAuthLib core, 16 files, `-Os`** | **12 475** |
| our entire ATmega328P image today, all capabilities | 11 700 |
| flash budget, ATmega328P | 16 384 |
| **budget remaining for a driver** | **4 684** |

The library's command layer alone is **2.7× the space left for it**, before any
HAL, before `atcacert`, before the software SHA fallback it wants, and before
the DICE work that is the point of having the element at all.

`--gc-sections` would trim unreachable functions, and the number above is
therefore an upper bound for the subset compiled. It does not change the
conclusion: the four largest objects are the ones every command path goes
through.

| file | bytes | reachable from |
|---|---|---|
| `calib_write.c` | 2 700 | provisioning, `PrivWrite`, config zone |
| `calib_read.c` | 2 584 | config zone, serial number, public key |
| `atca_iface.c` | 1 171 | every command |
| `calib_command.c` | 1 132 | every command |
| `calib_execution.c` | 990 | every command |
| `calib_basic.c` | 936 | init, wake, idle, sleep |

`atca_iface.c` + `calib_command.c` + `calib_execution.c` + `calib_basic.c` is
4 229 bytes of unavoidable core — 90 % of the entire remaining budget with not
one command issued.

## Finding 3 — there is no AVR HAL in the tree anyway

`lib/hal/` ships implementations for SAM, ESP32, Harmony (PIC32/ARM), Linux,
Windows, hidapi and FreeRTOS. `lib/hal/README.md` routes 8- and 16-bit parts —
"AVR, PIC16/18, PIC24/DSPIC" — through the MPLAB Code Configurator, a Microchip
IDE generator, not through a file in the repository.

So even in the world where the licence allowed it and the flash existed, we
would be writing the I²C HAL, the wake pulse and the timing ourselves. The thing
we would be adopting is the command framing and the CRC — the two smallest parts
of the job.

## What we write instead

Not a reimplementation of CryptoAuthLib. Six commands, which is what the DICE-HW
profile actually uses:

| command | why |
|---|---|
| `Wake` / `Idle` | the bit-banged low pulse plus tWHI; standard TWI cannot express it |
| `Read` | config zone, serial number, lock state |
| `Write` / `PrivWrite` | provisioning only, behind `tfwprov --commit` |
| `Lock` | provisioning only, irreversible |
| `Nonce` + `GenKey` / `Sign` | DeviceID and the attestation signature |
| `HMAC` | CDI = HMAC(UDS, H(firmware)) without the UDS leaving the element |
| `Random` | the TRNG — the entropy source DICE HW Requirements §6.2.4.1(6) wants |
| `Counter` | monotonic boot count and the anti-rollback floor |

Command framing is one packet layout and one CRC-16; the element's protocol is
small. The cost of owning it is bounded and the alternative is not available.

## What this deliberately does not do

- **No vendored copy, not even of a subset.** A subset of an
  exclusively-with-Microchip-products licence is still that licence.
- **No runtime choice between the two.** There is one driver.
- **No claim that our driver is more correct than Microchip's.** It is smaller
  and it is licensable. Those are the two properties that decided it.
