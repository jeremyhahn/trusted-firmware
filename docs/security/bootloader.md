# ag-boot, the trusted bootloader

> **Status: NORMATIVE** · Scope: `boot/`, `include/ag/boot.h`, `mk/boot.mk`,
> `src/third_party/ed25519-donna/` · Last verified: 2026-09-23
>
> One section of this page — "What does not fit" — records a measurement that
> contradicts a claim made elsewhere in these docs. The measurement is right and
> the claim has been corrected.

`ag-boot` runs before the application on every reset, with `BOOTRST`
programmed. It does four things and then jumps.

1. **Measures.** SHA-256 over the application flash. This is `FWHash`, the
   value signed attestation evidence carries.
2. **Verifies.** A signature over the image trailer, against a public key whose
   SHA-256 is pinned in reserved EEPROM.
3. **Checks the floor.** An image whose security version is below the recorded
   floor does not run.
4. **Derives the CDI.** DICE layer 0, handed to the application in SRAM.

When any of those refuses, it does not jump. It stays in an OTA loop, which is
the only recovery a part with no spare flash has.

## The three layouts

All three are in `include/ag/boot.h`, once, because each has a second party that
must agree with it byte for byte.

**The image trailer** is the last 128 bytes of the application region, at
`AG_BOOT_TRAILER_ADDR`. It carries the security version, the image length, the
measurement and the signature.

The signature covers the trailer's first 48 bytes — magic, layout version,
image version, image length and measurement — and **not the measurement alone**.
That is the difference between anti-rollback that works and anti-rollback an
attacker defeats with a four-byte edit: with only the measurement signed, a
genuine old image relabelled with a high version walks straight past the floor.
`test_a_relabelled_version_is_refused` is that case.

**Reserved EEPROM** `0x000`–`0x07F` holds the pinned public-key hash, the
software-tier rollback floor, the boot count, the boot state and the device
serial, with a CRC over the region.

The **hash** is in EEPROM and the **key** is in boot-section flash, deliberately.
A key held only in EEPROM is rotatable by anything that can write EEPROM; a key
held only in flash is rotatable by anything that can write flash. Splitting them
means replacing the bootloader replaces the key, and the EEPROM hash is what says
whether that replacement was authorised.

The CRC is **not** integrity against an attacker — anyone who can write the
region can recompute it. It is there for the failure
`fuses-and-lockbits.md` names: an EEPROM write on a sagging supply corrupts
rather than fails, and a silently corrupted floor is a downgrade that arrives by
accident. `BODLEVEL` is the mitigation; the CRC is how the bootloader notices
when `BODLEVEL` was not set.

**The CDI handoff** is 80 bytes at the top of SRAM, in `.noinit`, written by the
bootloader and read once by the application.

SRAM and not a call. The obvious alternative — the application asks the
bootloader for its CDI through a trampoline — is exactly what the tier forbids:
one BLS callable voids the BLB mode 3 read barrier from the inside, and the
software-tier UDS sits behind precisely that barrier. **No BLS callable is
exposed. Not one.**

## Anti-rollback

The version is a **security version number**, not a release number. It moves
when an image must never run again; a cosmetic release does not move it.

On the element tier the floor is the ATECC608A's monotonic counter, which cannot
be made to descend by anything — not a bug, not a sagging supply, not anyone
with the serial line. Without an element it is in reserved EEPROM, where it is
exactly as monotonic as the write that maintains it.

A single update may raise the floor by at most `AG_BOOT_FLOOR_MAX_STEP` (16).
On the element tier the floor advances one increment at a time out of a
device-lifetime budget of roughly two million, so an image stamped version
2000000 would spend the whole budget in one update and leave anti-rollback
permanently stuck — from a **signed** image, with no way back. The cost of the
bound is that a device cannot skip more than sixteen security versions in one
step and must be walked up through the intermediate ones.

## OTA, and why there is no A/B slot

An ATmega328P has 32 KB of flash: 4 KB of boot section, 28 KB of application.
Two application slots need 56 KB. Nothing in the board contract provides
external storage, and requiring some would make this a property of the bill of
materials rather than of the firmware.

So **an update is written in place**, and for the duration of the write the
device has no bootable application.

**The bootloader is the recovery.** Before the first page is erased the EEPROM
`boot_state` becomes `AG_BOOT_STATE_STAGED`, and it only returns to
`AG_BOOT_STATE_GOOD` once the whole image has been measured and its signature
has verified. A bootloader that finds `STAGED` does not jump; it stays in the
OTA loop. An interrupted update therefore leaves a device that is not running an
application and **is** still reachable, updatable and honest about its state.

The cost is stated rather than hidden: between the first erase and a successful
commit, a power cycle leaves the device in the bootloader rather than running
the old application. A deployment that cannot tolerate that window needs a part
with more flash or an external staging medium, and that is a hardware decision.

Four commands, answered only by the bootloader — `OTA_BEGIN`, `OTA_DATA`,
`OTA_END`, `OTA_STATUS`, plus `PING` and `RESET`. The application answers
`AG_ERR_UNKNOWN_CMD` to all of them, correctly: it cannot write flash, because
SPM executes only from the boot section and the only way to give it access
would be the trampoline this design forbids.

`OTA_END` succeeding means every **byte** arrived. It does not mean anything was
verified. Verification happens after, through the same `ag_boot_evaluate` the
cold boot path runs, against the same floor — one decision function, so an image
that would be refused at boot cannot be accepted at update.

## How secure boot fits: the element does the cryptography

**The boot section holds no cryptography at all.** Both the hash and the
signature check happen inside the ATECC608A, and what is left in the 4 KB
section is the boot decision, the EEPROM layout and the CDI handoff.

| board | boot section | image | spare |
|---|---:|---:|---:|
| ATmega328P | 4 096 | **3 426** | 670 |
| ATmega32U4 | 4 096 | **3 496** | 600 |
| ATmega2560 | 8 192 | **3 580** | 4 612 |
| ATmega2560, with OTA in the bootloader | 8 192 | **5 826** | 2 366 |

Reproduce with `make bootloader-check`, which builds every supported
configuration and fails if one stops fitting. The margin on a 4 KB part is the
kind of thing that disappears one commit at a time.

> These figures were 2 368 / 2 436 / 2 542 / 5 328 until the simavr suite ran
> against the image for the first time. `ag_boot_evaluate` was refusing every
> image with `AG_ERR_UNSUPPORTED` before it reached the signature — it asked the
> backend for a public key and the element backend correctly declines to produce
> one — so LTO proved `ag_boot_verify` unreachable and **deleted the verify path
> from the image**. The old numbers are the size of a bootloader with no
> verification in it. Both halves had tests: `test_boot_policy` supplies its own
> `ag_boot_pubkey`, `test_boot_element` asserts that the real one refuses.
> Nothing built the composition until something ran it.

### The two things that had to move

**The hash.** Software SHA-256 is 4 052 bytes, and the smallest useful core
around it is 5 688 with no verifier at all — already 1 592 over. The element has
a SHA engine, so `BOOT_MEASURE=atecc608a` streams the image through it and the
software implementation leaves the boot section entirely.

The digest is **identical**: SHA-256 is defined over the byte sequence, not over
how the bytes were delivered. `test_boot_element` asserts the element's output
against both the software hash and FIPS 180-4's published vector, so neither
implementation is the other's only oracle.

What it costs is time. The element hashes 64 bytes per command, so a 28 KB
application is about 450 round trips — each a bus transfer plus the part's own
SHA execution. Expect seconds, on every boot, and it cannot be cached: a
measurement is only evidence if it was taken this time. On the 2560, whose
section is 8 KB, use `BOOT_MEASURE=soft` instead — same digest, several hundred
times faster.

**The OTA path.** `BOOT_OTA=0` costs 2 220 bytes and is the only piece of a
bootloader that has somewhere else to live: an application can receive an image,
stage it and reboot, and this bootloader measures, verifies and commits it. What
is lost is the case where there is no working application to do the receiving —
so on a 4 KB part, recovery from a failed image needs a programmer.
`recovery/recovery_halt.c` says that by stopping, with interrupts off, rather
than timing out into an unverified image.

### Verify in STORED mode, and why that is the whole point

The element's `Verify` has an external mode that takes the public key in the
command packet. That would put the image-signing key in boot-section flash,
where an attacker who can rewrite flash substitutes their own and signs whatever
they like — and surviving an attacker who reached the flash is the only reason
to verify at boot.

Stored mode names a **slot**. The key is written at provisioning and locked, and
after that no command on the part can change it. Rewriting every byte of the
MCU's flash does not help: the element still verifies against the key the
manufacturer put there.

It is also smaller — 64 bytes of signature on the wire instead of 128 of
signature and key.

**The reserved-EEPROM key pin does not apply on this tier, and that is not a
weakening.** The pin exists because replacing the bootloader replaces the key it
verifies against, so the SHA-256 in EEPROM is what makes a boot-section swap
detectable from inside the bootloader. A locked element slot cannot have its key
replaced at all — not by a rewritten boot section, not by anything on the part —
so there is nothing for the hash to catch. `ag_boot_pubkey` answers
`AG_ERR_UNSUPPORTED`, `ag_boot_evaluate` reads that as a tier capability answer
and passes no key to `ag_boot_verify`, and the element backend refuses a
caller-supplied key so that a future caller cannot quietly reintroduce one. It
is the same shape as `ag_boot_floor_read` on a tier that keeps no floor of its
own.

### What this costs in honesty

**Secure boot on AVR requires an ATECC608A.** A board without one gets measured
boot: the bootloader computes `FWHash` and the platform refuses to trust a
device whose measurement does not match the golden build. That is the fleet
property, and it is the one that matters for attestation — but a tampered image
still *runs* locally until the next attestation, and that difference must not be
glossed.

There is no arrangement in which a 4 KB AVR boot section verifies a signature in
software. The numbers below are why.

## What does not fit

> The numbers below are reproduced by `make bootloader-budget` and
> `make boot-core-size`. They are measurements, not estimates.

### ed25519-donna

`avr-gcc 14.2.0`, `-Os`, `--gc-sections`, nothing reachable but
`ed25519_sign_open`:

| | ed25519-donna | the ATmega328P has |
|---|---:|---:|
| flash, verify path only | **72 930 bytes** | 4 096 in the boot section |
| `ed25519_sign_open` stack frame | **2 950 bytes** | 2 048 of SRAM, total |
| `ge25519_niels_sliding_multiples`, one rodata table | 3 840 bytes | — |

`BOOTSZ` at 2048 words **is the ATmega328P's largest setting**. There is no
bigger boot section to select, so this is not a budget that can be raised. It is
17.8× the whole boot section and 2.2× the part's entire flash.

Two further facts about donna on this target:

- With `ED25519_REFHASH` — upstream's own SHA-512, which Ed25519 requires and
  this tree does not otherwise have — the translation unit is **154 654 bytes**
  before linking.
- It assumes a 32-bit `int`. `reduce_mask_25 = (1 << 25) - 1` in
  `curve25519-donna-32bit.h` is evaluated in `int`, which is **16 bits** on AVR.
  The field arithmetic is wrong on this target before it is too big for it.

### The bootloader core, without any signature backend

`make boot-core-size`, everything except the verify seam, linked with LTO and
`--gc-sections`:

| component | bytes |
|---|---:|
| SHA-256 `transform` | 2 240 |
| `ota_loop` — the framed OTA path over the polled USART | 1 390 |
| `ag_boot_evaluate` — measure, compare, pin, rollback | 1 238 |
| `hand_over_and_jump` — CDI derivation and the handoff | 802 |
| `main`, `ag_sha256_final`, `ag_sha256_update`, the K table, the rest | 2 238 |
| **total** | **7 908** |
| RAM | 632 |

**The core alone is 3 812 bytes over a 4 096-byte boot section**, before one
byte of signature verification.

So the finding is not "Ed25519 is too big". It is that **a 4 KB AVR boot section
cannot hold SHA-256, HMAC, a framed OTA path over the UART, the reserved-EEPROM
layout and the boot decision** — and the signature primitive is on top of that.

### What has NOT been done about it

No substitute primitive has been chosen and none has been quietly swapped in.
Changing the signature scheme changes what the device's security claim means,
and that is a decision with an owner, not a detail to settle inside a seam.

The options, with their costs, so the decision can be taken on evidence:

| option | cost |
|---|---|
| **A larger part.** The ATmega2560's boot section is 8 192 bytes and it has 8 KB of SRAM. | The core fits with room. Ed25519 still does not — donna is 72 930 bytes against 8 192. It changes the part, not the answer. |
| **Verification inside the ATECC608A.** The element has an ECDSA P-256 `Verify` command in external mode. The boot section would carry the I²C driver and a command packet, not a curve implementation. | Asymmetric verification with almost no code. But it is ECDSA P-256, not Ed25519, and it works **only on the element tier** — a software-tier device gets nothing. |
| **Symmetric image authentication.** HMAC-SHA256 over the trailer, under a key the device already holds. The code is already there for the CDI. | Fits easily. But the verifier holds the key, so anything that reads the key can mint an update. This is the TCG DICE Symmetric posture the software tier already reports as `notSecure`, extended to the update path — an honest option, and a strictly weaker one. |
| **A hand-written AVR Ed25519.** The published assembly implementations are 15–20 KB. | Still 4× over, on a part whose boot section cannot grow. Not a route. |
| **Ship no bootloader on the 328P.** Keep ag-boot for parts whose boot section can hold it. | Honest, and it means the 328P has no measured boot — which is most of what the DICE-SW tier claims. |

Nothing below the line is decided. `BOOT_SIG` selects a backend at link time and
currently has one value, so adding a second is a file and a case in `mk/boot.mk`.

## The build

```sh
make bootloader BOARD=atmega328p BOOT_TIER=atecc608a \
     BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0
make bootloader-check                       # every supported configuration
make bootloader-budget BOARD=atmega328p     # the per-component breakdown
make boot-core-size BOARD=atmega328p        # the core, without a backend
make boot-test                              # the host suite
make bootloader-reproducible                # two builds, byte-identical
make boot-flash                             # over ISP, NOT over the application bootloader
```

`boot-size-check` is a gate and a prerequisite of `bootloader`, exactly as
`size-check` is for the application. The budget comes from the board profile and
**is not raised to meet an image**.

The reproducibility flags are the application's: `SOURCE_DATE_EPOCH`,
`-ffile-prefix-map`, `-frandom-seed`, a sorted deterministic link order, and a
`make lint` grep for `__DATE__`/`__TIME__` that now covers `boot/` too. The
reason is stronger here than for the application: the bootloader is what a
verifier must be able to identify, and one nobody can rebuild byte for byte is a
root of trust nobody can audit.

`boot/` is covered by the same preprocessor-conditional grep as `src/core`,
`src/proto` and `src/caps`, with `boot/hal/avr` exempt on the same terms as
`src/hal/avr`. There are two conditionals under that exemption, both facts about
the silicon: the far-flash read, required above 64 KB, and clearing `EIND`
before the jump.

**`EIND`, and why forgetting it is not a crash.** Above 128 KB the program
counter is three bytes and an indirect call is `eicall`, whose target is
`EIND:Z`. The C runtime sets `EIND` to the 128 KB page `__vectors` is in, which
for a bootloader at `0x3E000` is page 1 — so `((void(*)(void))0)()` jumps to
`0x20000`, not to `0`. On a Mega that is erased flash: `0xFF` decodes as a long
run of `SBRS`, the part walks the upper half of its flash and falls back into
the bootloader, which measures, verifies and jumps again, forever, with nothing
on the wire to say why. `ag_boot_jump_application` clears `EIND` first. The
simavr suite is what found this; nothing on the host can.

## What is shared with the application, and why

| shared | because |
|---|---|
| `src/crypto/sha256.c` (`BOOT_MEASURE=soft` only) | the measurement must be the hash the application reports and a verifier recomputes |
| `src/proto/frame.c`, `crc16.c` | one codec, so OTA frames and application frames cannot drift apart |
| `src/hal/avr/hal_early.c` | one "clear MCUSR and disarm the watchdog"; the copy that is wrong bootloops the part |
| `src/hal/avr/avr_usart_regs.h` | one place a board that moved to USART1 has to be remembered |
| `src/crypto/atecc608a/atecc.c` | one implementation of a packet format whose CRC covers the count byte onward |

The `.init3` sharing matters more in the bootloader than in the application.
With `BOOTRST` programmed, that code is the first thing to run after **every**
watchdog bite — including the one `ag_hal_reset()` deliberately causes. A
bootloader that did not disarm the watchdog would take the reset it was asked
for and never get far enough to hand control back.

## Testing

`make boot-test` — 81 assertions across seven suites on x86 against
`test/mock/boot_hal_mock.c`:

- the trailer, the rollback rule and the floor advance
- the reserved-EEPROM layout, including every corruption case
- the measurement against a direct SHA-256 of the same bytes
- the CDI handoff round trip, including a block a stack grew into
- the OTA state machine, including a truncated transfer and a failing flash
- the four commands, each once for working and once for a bad payload length
- **Ed25519 against RFC 8032 §7.1**, valid and tampered, as literal hex

`test_boot_policy.c` proves the three behaviours that matter — a correctly
signed image is accepted, a tampered one is refused, a downgrade is refused —
against the real primitive, with a real RFC 8032 key and a real signature, over
a real flash array. Nothing is stubbed there but the hardware.

`make integration-test-boot` — `test/integration/simavr/boot_harness.c` asks the
same three questions of the real `.elf` on emulated silicon, and it is in
`make ci`.

It loads `ag-boot.elf` at its link address, sets the program counter there
because `BOOTRST` is programmed, puts the application `.elf` this same CI run
built into the flash below it with a trailer, provisions the reserved EEPROM,
and hangs an **ATECC608A on the emulated TWI bus** — a slave attached to
simavr's `avr_twi` IRQs, so the element protocol arrives through
`src/hal/avr/hal_bus.c`'s real TWI state machine rather than through the
`ag_hal_i2c_xfer` seam. That is the difference from `test/model/atecc608a.c`,
which models the same part above the driver; neither subsumes the other.

Because the element is below the driver, the harness can assert things no
status byte carries: that the bus was clocked slowly enough for the wake pulse
to work, that the driver sent one whole 64-byte `SHA Update` per block and
exactly one tail, that a tampered image cost a full measurement and **no**
`Verify`, and that a downgrade was refused before the element was asked to hash
anything. Every wait is bounded in cycles, so a bootloader that stops answering
fails the job rather than hanging it.

It runs on `atmega2560` with `BOOT_OTA=1` — a row of `BOOT_SUPPORTED`, and the
only one whose bootloader can say *why* it refused. On a 4 KB part recovery is
`recovery_halt.c` and a refusing device is deliberately mute, which would make
"tampered" and "downgrade" the same observation.

## Still outstanding

**The application does not yet read the handoff block.** The bootloader computes
the CDI and places it; `src/crypto/soft/provider.c` still derives its own from
`ag_crypto_soft_uds()`, which returns NULL. Wiring it up changes what the
software tier's `ag_crypto_init` derives an identity from, so it is a separate,
reviewable change.

It also resolves a contradiction already in the tree.
`src/crypto/soft/uds_flash.c` says the UDS "lives in the boot section behind BLB
lock bits" and that the application will read it there. **Under BLB1 mode 3 the
application cannot read boot-section flash at all** — that is the whole point of
the barrier. So the application can never receive the UDS; it can only receive
the CDI, which is what the handoff block carries and why it carries that rather
than the secret.

The application's linker must place its reader at the same address the
bootloader writes: `.noinit` at `RAMEND + 1 - 80`, with `__stack` below it.
