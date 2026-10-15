# ADR: the ESP-IDF integration boundary and transport

> **Status: ACCEPTED (2026-10-15), signed off by the Company Agent at 03:20Z** · Issue: [#16](../../../../issues/16)
> · Implementation: [#48](../../../../issues/48)
> · Scope: trusted-firmware (HAL, boot HAL, board profiles, build) · Date: 2026-10-15
>
> The owner lifted the AVR gate for `esp32-p4` on 2026-10-15 (00:32Z) and ordered
> the port onto Espressif's QEMU until the board arrives. This ADR records the
> boundary that port is built on. The Company Agent accepted it on 2026-10-15
> (03:20Z); the sign-off is at the end.

## The question

The firmware core is portable C behind a link-time HAL (`include/tfw/hal.h`) and
a boot HAL (`boot/boot_hal.h`). The ESP32-P4 brings an SDK that owns its boot
chain: ESP-IDF's ROM and second-stage bootloader, secure boot v2, flash
encryption, the partition table, OTA slots, eFuses, and the HMAC and DS
peripherals. The SPEC row `esp32-p4` says those are **composed, never
re-implemented** (L2). So where does ESP-IDF end and this repository begin? In
particular:

1. how the build meets ESP-IDF's CMake, and where the glue lives;
2. what is composed from ESP-IDF and what is ours;
3. where layer 0 (the measuring, CDI-deriving layer) runs relative to the
   second-stage bootloader;
4. how a DeviceID held by the DS peripheral squares with CDI derivation;
5. how the wire reaches the device over Ethernet;
6. the boot HAL's geometry, and NVM on partitions;
7. which emulator stands in for the board, and on what evidence;
8. which board ids it takes.

## Context

### What the core expects

- **One source list per image.** `mk/firmware.mk` decides from the board
  profile and the crypto tier which files make up an image (`FW_SRCS`). Features
  are whole files in or out, never `#if` outside the four permitted places.
- **A superloop.** `main()` never returns; it polls the transport, the
  scheduler and the system module.
- **A byte link.** `tfw_hal_uart_*` carries the framed wire protocol
  (`src/proto/frame.c`): SOF, length, sequence, CRC-16, resynchronisation on
  the next SOF.
- **A handoff.** Layer 0 leaves an 80-byte block (`include/tfw/boot.h`) that
  `src/core/handoff.c` parses once and zeroes: tier, CDI, measurement, image
  version, reset cause.
- **The element-tier precedent.** On the ATECC608A the DeviceID key is
  hardware-resident and is *not* derived from the CDI; the CDI comes from the
  measurement and keys everything else (`docs/dice-attestation/architecture.md`).

### What the emulator offers

There is no `esp32p4` machine in Espressif's QEMU. The pinned build
(`esp_develop_9.2.2_20260417`) has `esp32`, `esp32c3` and `esp32s3`. Their QOM
trees (`info qom-tree` on the monitor) list these peripherals, which decide what
each can test:

| Machine | HMAC | DS | RSA | SHA | AES / XTS | eFuse (file-backed) | OpenCores Ethernet |
|---|---|---|---|---|---|---|---|
| `esp32c3` | `misc.esp32c3.hmac` | `misc.esp32c3.ds` | yes | yes | yes / yes | `nvram.esp32c3.efuse` | `open_eth` |
| `esp32` | — | — | yes | yes | yes / flash encryption | `nvram.esp32.efuse` | `open_eth` |

The ESP32-C3 is the only machine with the HMAC and DS peripherals, which the
identity path needs. It is RISC-V, like the P4, and its HMAC, DS and secure
boot v2 are the same IP generation as the P4's. The ESP32 (Xtensa) has neither
HMAC nor DS, but it has secure boot v2 (revision 3) and the same Ethernet
model, so it proves the port is not RISC-V-only and that the `none` tier boots
under secure boot.

### Toolchain pins

Installed outside the clone by ESP-IDF's own `idf_tools.py`, never committed,
and checked by `make esp32-build` / `make esp32-qemu-test`, which refuse any
other version (`mk/arch/esp32.mk`):

| Tool | Pin |
|---|---|
| ESP-IDF | `v5.5.5` (`git clone -b v5.5.5 --recursive`, then `install.sh esp32,esp32c3,esp32p4`) |
| Espressif QEMU | `esp_develop_9.2.2_20260417` (`idf_tools.py install qemu-riscv32 qemu-xtensa`) |
| GCC | `riscv32-esp-elf` and `xtensa-esp-elf` esp-14.2.0_20260121 (ESP-IDF's pin) |
| CMake / Ninja | 3.30.2 / 1.12.1 (`idf_tools.py install cmake ninja`) |
| DS parameter encoder | `esp-secure-cert-tool==2.6.1`, in ESP-IDF's Python environment |

A measurement is only reproducible from source with the same toolchain, so the
pin is part of the trust story, not a convenience. `CONFIG_APP_REPRODUCIBLE_BUILD`
is on; two builds of one commit gave the same image digest.

## Options

### 1. The build glue

- **(a) A component with its own source list.** The usual ESP-IDF layout. It
  is a second answer to "which files make up an image", free to drift from
  `mk/firmware.mk`, which every other architecture and the board sweep use.
- **(b) The core as a static library built by our Makefile, linked into an
  ESP-IDF project.** It needs ESP-IDF's exact compile flags, sysroot and
  per-target ABI options (`-mlongcalls` on Xtensa, among others) reproduced
  outside ESP-IDF. That is ESP-IDF's knowledge, re-implemented.
- **(c) An ESP-IDF project under `mk/arch/esp32/`, fed `FW_SRCS` by make.**
  `make esp32-build BOARD=<board>` evaluates `mk/firmware.mk` as for any board,
  then runs `idf.py` with the list (`-DTFW_SRCS`), the board directory and the
  feature defines. The project's one component registers exactly those
  sources. Layer 0 is a second, bootloader-side component under
  `boot/hal/esp32/bootloader_components/main`, added through ESP-IDF's
  `BOOTLOADER_EXTRA_COMPONENT_DIRS`.
- **(d) A thin consumer repository** (the escape hatch in `architecture.md`
  §1). Not needed: the ESP-IDF tree stays outside the clone, and the glue is a
  few small files.

### 2. Layer 0

- **(a) In the application.** An image that measures itself proves nothing.
- **(b) ESP-IDF's bootloader hooks** (`bootloader_before_init`,
  `bootloader_after_init`). They run before a slot is chosen, so they cannot
  measure the image about to boot.
- **(c) Replace the second-stage bootloader's entry, `call_start_cpu0`.**
  ESP-IDF supports this (`examples/custom_bootloader/bootloader_override`).
  Layer 0 calls ESP-IDF's own `bootloader_init`, partition-table load and slot
  selection, measures and hands off, then calls ESP-IDF's
  `bootloader_utility_load_boot_image`, which loads and jumps.

### 3. The DeviceID

- **(a) Derived from the CDI**, as DICE describes. Not possible with DS: the DS
  ciphertext is computed from the private key at provisioning, under an eFuse
  HMAC key, so the key cannot be re-derived at boot.
- **(b) DS-held RSA, independent of the CDI.** The element-tier model, on the
  part's own key store.
- **(c) The P4's ECDSA peripheral**, a P-256 key in an eFuse block that cannot
  be read out. It would match the wire's 64-byte signature and the P-256
  alias template unchanged. The SPEC row names DS; QEMU models no ECDSA
  peripheral, so nothing here could test it.

### 4. The Ethernet transport

- **(a) UDP datagrams**, one frame each. Needs loss handling, reordering and
  retransmission the serial link never needed.
- **(b) TCP, the existing framing on the byte stream.** TCP supplies ordering
  and retransmission; the framing's CRC and resynchronisation still guard
  against a host bug. Nothing in `src/proto` or `sdk/go` changes.
- **(c) A new framing (length-prefixed, or TLS records).** A wire change for no
  gain at this layer; authentication is a separate decision (see Consequences).

### 5. The emulation target

- **(a) `esp32c3` only.** Covers identity, but leaves the Xtensa build unproved.
- **(b) `esp32c3` and `esp32`.** One board id, two machines (see below).
- **(c) Wait for an `esp32p4` QEMU machine.** None is announced.

## Recommendation

| # | Decision |
|---|---|
| 1 | **Glue (option 1c).** `mk/arch/esp32/` holds the ESP-IDF project (`CMakeLists.txt`, `main/CMakeLists.txt`, `sdkconfig.defaults`); `mk/arch/esp32-targets.mk` holds `esp32-build` and `esp32-qemu-test`. `FW_SRCS` is the only source list. `make firmware BOARD=<esp32 board>` refuses and names `esp32-build`. `make ci` is unchanged and runs none of this; the separate `esp32` CI job runs both targets (see Consequences). |
| 2 | **Composed from ESP-IDF:** ROM and second-stage bootloader; secure boot v2 (RSA-3072, signing by `espsecure.py`, verification by `esp_image_verify`); flash encryption (release mode on the P4 board; off in QEMU); the partition table and OTA slot selection; the eFuse API; ROM HMAC (`ets_hmac_calculate_message`); DS (`esp_ds_sign`); `esp_partition`; `esp_eth` and lwIP; the GPIO, LEDC, ADC one-shot, I2C master and SPI master drivers; `esp_timer`; FreeRTOS as the host of one task. **Ours:** layer 0 (`boot/hal/esp32/boot_l0.c`), the boot HAL (`boot/hal/esp32/boot_hal_esp32.c`), the HAL (`src/hal/esp32/`), the `hmac-ds` crypto provider (`src/hal/esp32/crypto_hmac_ds.c`), the TCP byte link, the DeviceID record format, and the QEMU harness. |
| 3 | **Layer 0 replaces the second-stage bootloader's entry (option 2c).** The ROM verifies the bootloader (secure boot v2) and is the root. Layer 0 verifies the slot it is about to boot with `esp_image_verify`; the image's appended SHA-256, covered by the signature, is the **measurement**. The CDI is `HMAC-SHA256(UDS, measurement)`, computed by the HMAC peripheral from the eFuse key whose purpose is `HMAC_UP`, which is the UDS. The handoff goes into ESP-IDF's RTC-retained memory: its custom area (`CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE=0x50`), kept out of the record's CRC. A slot that fails verification hands off **no tier**, so a fallback slot boots without a CDI rather than with one derived for another image. ESP-IDF's loader then verifies again and loads: a second verification, about 40 ms in QEMU, kept rather than re-implementing the loader. |
| 4 | **DeviceID (option 3b).** An RSA key, 3072-bit on the C3 and up to 4096 on the P4, held by DS: its `esp_ds_data_t` ciphertext sits in the `tfw_id` partition, under the eFuse key whose purpose is `HMAC_DOWN_DIGITAL_SIGNATURE`. It is independent of the CDI, as on the element tier, and survives firmware updates; the measurement reaches a verifier through evidence the DeviceID signs. Each boot the provider signs a fixed self-test digest through DS and verifies it against the record's modulus. **The wire cannot carry it yet**: its signatures are 64 bytes and its templates P-256. So the tier reports `TFW_CRYPTO_SECURE_ELEMENT` (an on-die key store) with no algorithms, `ATTEST` is off, and sign and pubkey refuse. The ECDSA peripheral (3c) is the alternative to weigh at P4 bring-up (follow-up 4). |
| 5 | **Transport (option 4b).** TCP, port **21574** (`0x5446`, "TF"; IANA lists 21555–21589 as unassigned), the existing framing unchanged on the stream, behind `tfw_hal_uart_*`. One host at a time; a new connection replaces the old one, and bytes sent with no host connected are dropped, as on an unplugged UART. The address comes from DHCP; a static address, when one is wanted, comes from the board configuration, never a literal in code. A host uses `sdk/go` on a `net.Conn` with `WithBootWait(0)`, since a connection does not reset the device. **The AVR Ethernet transport (#55, W5100/W5500) should take the same port and framing**, so that one host path serves both families. On AVR the port is configuration: the AVR Ethernet transport ([`docs/ethernet-transport/`](../ethernet-transport/)) leaves it to "the port the endpoint listens on", and 21574 is this ADR's proposal for that configured value. |
| 6 | **Geometry and NVM on partitions.** The boot HAL's flash page is the 4096-byte sector. The partition table sits at `0x10000`, because the RSA secure-boot bootloader with layer 0 (44–52 KiB) outgrows the default `0x8000`. Custom data subtypes: `tfw_plat` (`0x40`), the platform block, read-only to the application; `tfw_id` (`0x41`), the DS record; `tfw_eeprom` (`0x42`), the EEPROM capability, written by sector read-modify-write. Then ESP-IDF's `otadata`, `ota_0` and `ota_1`. `tfw_plat` and `tfw_eeprom` are marked `encrypted`; `tfw_id` is not, because it holds only ciphertext and a public modulus. |
| 7 | **Emulation target (option 5b).** `esp32c3` carries secure boot v2, the HMAC CDI, the DS DeviceID and the wire. `esp32` carries secure boot v2 and the wire on the `none` tier. Evidence: the QOM table above, and `make esp32-qemu-test` (see Consequences). |
| 8 | **Board ids, the fewest:** `0x80` `esp32p4-poe`, and `0x81` `esp32-qemu` for both machines. CAPS already tells a host the one fact that differs between the machines, the crypto tier. A board is a build target, not a part, so the QEMU board is a board. |

## Consequences

- **What `make esp32-qemu-test` proves**, on each machine, with keys generated
  into the build directory at test time and never committed:
  - eFuses burned through QEMU's download mode with `espefuse.py`: the
    secure-boot key digest and enable bit; on the C3 also the UDS (`HMAC_UP`,
    KEY1) and the DS key (`HMAC_DOWN_DIGITAL_SIGNATURE`, KEY2);
  - the ROM verifies the bootloader; the bootloader verifies the app with
    RSA-PSS; layer 0's measurement equals the image's own SHA-256;
  - the C3's CDI fingerprint equals `HMAC-SHA256(UDS, measurement)` computed
    on the host, and the DeviceID fingerprint equals `SHA-256(n ‖ e)` of the
    generated key, with the DS self-test verified;
  - all three are identical across a soft reset and a cold start of a new QEMU
    on the same flash and eFuses; an EEPROM mark written over the wire
    survives both;
  - `sdk/go` answers PING, CAPS (board `0x81`, the expected tier) and SYS_INFO
    over the emulated Ethernet;
  - an app with one byte flipped and its checksum and SHA-256 fixed up, so
    that only the signature can notice, is refused: no measurement, no wire.
- **A gap in the ESP32 HMAC model.** ESP-IDF v5.5.5 offers no eFuse or
  register lock that stops a later stage from using the `HMAC_UP` key until
  the next reset. A signed application could
  compute the CDI of another measurement. Secure boot v2 limits this to signed
  code; it is a weaker property than a UDS that only layer 0 can reach, and is
  stated here rather than hidden.
- **Entropy.** `tfw_crypto_random` is `esp_fill_random`. ESP-IDF documents its
  output as true random only while a noise source runs (the radio, or the SAR
  ADC through `bootloader_random_enable`). With Wi-Fi off, this must be
  settled before any key is generated on the device (follow-up 4).
- **Flash encryption** is on in the P4 profile (release mode) and off in QEMU.
  Nothing in QEMU tests it.
- **OTA is not on the wire yet.** `RESET(1)` is refused with `UNSUPPORTED`, as
  on a board with no boot section (`ARCH_UPDATE_REQUEST := none`), not
  silently turned into a soft reset.
- **The EEPROM capability wears a flash sector per write**, about 100 000
  cycles. Acceptable for configuration; not for counters.
- **The wire is unauthenticated on a network,** as it is on a serial cable,
  but a network reaches further. Until an authenticated transport exists, a
  deployment keeps the device on a management network.
- **The C6 co-processor** on the P4 board is held in reset from the first
  instruction of `tfw_hal_init` (`TFW_ESP_COPROC_EN_GPIO`, latched with
  `gpio_hold_en`), so no firmware runs on it and its radio never starts.
- **CI covers the port in its own job, not in `make ci`.** The `make ci` image
  (`docker/Dockerfile.ci`) has no ESP-IDF, and a gate that cannot run would be
  a failure, not a skip. So the `esp32` job in `.github/workflows/ci.yml`
  builds `docker/Dockerfile.esp32` instead: Espressif's `espressif/idf:v5.5.5`
  image pinned by digest, Espressif QEMU `esp_develop_9.2.2_20260417`
  (`qemu-riscv32`, `qemu-xtensa`), CMake, Ninja, `esp-secure-cert-tool` 2.6.1
  and Go. In that image it runs `make esp32-build BOARD=esp32p4-poe`, the only
  place the P4 image is built, since no emulator exists for it, and then
  `make esp32-qemu-test`, which builds `esp32-qemu` for both machines and runs
  the checks above on `esp32c3` and on `esp32`. The job has no skip guard: a
  missing ESP-IDF or QEMU fails it. The `CI Gate` job requires `esp32` to
  succeed, as it does every other job.

## Follow-up issues (proposed, not filed)

1. **ESP32-P4 bring-up on the PoE board.** Confirm every pin in
   `boards/esp32p4-poe/board_profile.h` against the schematic (RMII, MDIO, PHY
   reset, the C6 enable), then the first boot on silicon: secure boot v2 and
   flash encryption in release mode burned for real, the IP101 link, and the
   QEMU suite's checks repeated on hardware.
2. **A CI runner image with ESP-IDF v5.5.5 and Espressif QEMU.** Done by
   PR #68: the `esp32` CI job, built from `docker/Dockerfile.esp32`.
3. **An RSA DeviceID on the wire, and its profile in go-xkms**, or the owner's
   choice of the P4's ECDSA peripheral instead. Either way, an ADR first: an
   extended signature length (384–512 bytes, so paged) and an RSA alias or
   DeviceID profile in go-xkms; or a P-256 eFuse key that keeps the wire as it
   is but has no emulator.
4. **On-device provisioning for ESP32.** `PROV_GENKEY` returns a 64-byte
   point and cannot carry an RSA modulus. The DS ciphertext is computed on the
   bench today (`esp_secure_cert`'s encoder), with the private key in hand.
   This needs a provisioning flow, and settling the entropy source, before any
   key is generated on the part.
5. **OTA through ESP-IDF's `esp_ota` and `secure_version`.** An update
   session on the wire, anti-rollback from the eFuse counter, and
   `ARCH_UPDATE_REQUEST` dropped once it exists.
6. **Reset causes the HAL cannot name yet** (#10 follow-on). `tfw_reset_cause_t`
   has no value for a software reset or a panic; ESP-IDF reports both, and the
   port maps them to unknown.
7. **Boot geometry beyond 16-bit pages** (#11 follow-on). The OTA page and its
   counters were sized for 256-byte AVR pages; a 4 KB sector and multi-megabyte
   slots want wider fields before the OTA session is ported.
8. **Pulse capture with RMT** instead of polling `esp_timer`.
9. **A static address from the board configuration**, for networks without
   DHCP.
10. **`sdk/go` board names for `0x80` and `0x81`** (`sdk/go/boardid.go`), held
    back here because `sdk/go` was out of this change's scope.

## Sign-off

```
Decision: ACCEPTED as proposed
Options chosen: 1c glue, 2c layer 0, 3b DS DeviceID, 4b TCP framing, 5b esp32c3+esp32
Board ids: 0x80 esp32p4-poe, 0x81 esp32-qemu
Signed off by: Company Agent, 2026-10-15 03:20Z, recorded in the company rulings; the owner can overrule
Date: 2026-10-15
Notes:
```
