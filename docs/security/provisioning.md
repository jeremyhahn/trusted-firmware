# Provisioning

> **Status: NORMATIVE** · Scope: `tools/agprov`, `src/crypto/atecc608a/`,
> `docs/security/fuses-and-lockbits.md` · Last verified: 2026-09-22
>
> Every step below is reversible **until** the lock step. After it, nothing is.

Provisioning is what turns a part into a device with an identity. It happens
once, on a bench or a jig, never over the network and never from the firmware
itself.

## What a device has afterwards

| | Where | Reversible |
|---|---|---|
| Fuses and lock bits | the MCU | no (chip erase clears lock bits and the flash with them) |
| Bootloader and application | flash | yes, until lock |
| Device serial | EEPROM `0x000`–`0x07F`, platform-reserved | yes, until lock |
| Anti-rollback floor | the element's monotonic counter | **no — it only counts up** |
| UDS | element slot 1, never-readable | no, after zone lock |
| DeviceID key | element slot 0, non-exportable | no, after zone lock |
| EAB credential | the platform's enrollment store, not the device | yes |

## The sequence

```sh
# 1. Fuses. BOOTRST, BOOTSZ, BODLEVEL on, DWEN off.
#    See fuses-and-lockbits.md -- BOD is a security setting here, because an
#    EEPROM write on a sagging supply corrupts rather than fails.
avrdude -c <prog> -p <part> -U lfuse:w:...:m -U hfuse:w:...:m -U efuse:w:...:m

# 2. The images. Bootloader first, then the application.
make BOARD=atmega328p CRYPTO=atecc608a firmware
make BOARD=atmega328p flash

# 3. Plan the element configuration and change nothing.
agprov --dry-run --config templates/atecc608a-dice.yaml /dev/ttyUSB0

# 4. Write it, on a SPARE PART first. This is the procedure, not a precaution.
agprov --commit --config templates/atecc608a-dice.yaml /dev/ttyUSB0 --yes-i-mean-it

# 5. Read it back and compare.
agprov --verify --config templates/atecc608a-dice.yaml /dev/ttyUSB0

# 6. Lock bits. Only now.
avrdude -c <prog> -p <part> -U lock:w:0x0C:m
```

Step 5 is not optional and is not a formality. `--commit` reports what the
element acknowledged; `--verify` reports what the element actually holds. On a
part that cannot be unlocked, those are different questions.

## What `--commit` does, in order

1. **Wake and identify.** Confirms an ATECC608A is present and reports its
   revision and serial number. A part that does not answer the wake at 100 kHz
   is usually a bus running at 400 kHz — the wake pulse is the low period of a
   byte written to address zero, which is 22 µs at 400 kHz and below the 60 µs
   the part requires.
2. **Write the config zone** from the template: slot 0 as a non-exportable P-256
   private key, slot 1 as a never-readable HMAC key, the two monotonic counters,
   and the slot access policies.
3. **Lock the config zone.** *Irreversible.*
4. **Generate the DeviceID key** in slot 0 (`GenKey`, private mode) and read
   back its public point. The private half never exists outside the element.
5. **Write the UDS** into slot 1 from the element's own TRNG, never from the
   host. A UDS the host generated is a UDS the host has seen.
6. **Lock the data zone.** *Irreversible.*
7. **Write the device serial** into the platform-reserved EEPROM region.
8. **Record the provisioning entry**: element serial, DeviceID public key,
   firmware hash, template hash, timestamp, operator.

## The provisioning record is the authority, not the device

A device cannot prove it was provisioned correctly — anything it reports about
its own fuses or lock state is something an attacker with code execution could
also report. So the authority is the record written at step 8 and held by the
platform, and the device's job is to prove it holds the key that record names.

That is what the attestation exchange checks: a nonce-bound signature over a
domain-separated transcript, verified against the DeviceID public key the record
carries.

## Enrollment comes after, and elsewhere

Provisioning gives the device a key. **Enrollment** gives it a certificate, over
ACME `device-attest-01` with an EAB credential, and that is go-trusted-platform's
job rather than this repo's. The split matters: a device can be provisioned in a
factory with no network and enrolled later in the environment it will run in.

## Re-provisioning

There is no re-provisioning of a locked element. The options are:

- **A1 DeviceID (default):** a firmware or bootloader update does **not** break
  identity, because the measurement binding lives in the per-boot Alias key
  rather than in the DeviceID.
- **A2 DeviceID (`CONFIG_DICE_STRICT_DEVICEID`):** the DeviceID is CDI-derived,
  so a bootloader update *does* force re-provisioning — which means a new
  element, which means a new board. Choose A2 only where that is acceptable.

## Failure modes worth knowing before you hit them

| Symptom | Cause |
|---|---|
| the element never answers | bus above 100 kHz, so the wake pulse is too short |
| wake succeeds, every command returns `AG_ERR_CRC` | the CRC included the word address; it covers the count byte onward |
| commands return status `0x0F` | preconditions not met — usually a zone not yet locked, or a slot policy that forbids the operation |
| `--verify` disagrees with `--commit` | the template and the part disagree; the part wins and the board is scrap if the zones are locked |
| attestation returns `AG_ERR_NOT_PROVISIONED` | the element is present but has no UDS — steps 5 and 6 did not complete |

## What this deliberately does not do

- **No provisioning over the network.** There is no remote path to write a UDS
  or a key. Provisioning requires physical access, which is the assumption the
  whole trust model rests on.
- **No host-generated secrets.** The UDS comes from the element's TRNG. The
  software tier is the exception and is marked `notSecure` for exactly this
  reason among others.
- **No unlock, ever.** See `fuses-and-lockbits.md`. A locked element is retired
  with the board, and decommissioning means revoking the certificate and
  deleting the record, not reclaiming the part.
