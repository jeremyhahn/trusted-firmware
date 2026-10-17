# ADR: onboarding the Nordic nRF54L15-DK

> **Status: PROPOSED** · Issue: #129 · SPEC row: `nrf54l15` (future)
> · Related: ADR #14 ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)),
> ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> the board profile [`../boards/nrf54l15-dk.md`](../boards/nrf54l15-dk.md)
> · Scope: trusted-firmware only · Last verified: 2026-10-17
>
> Documentation only (owner, 2026-10-17, queue item 26: each arrived board gets
> one docs-only onboarding ADR; AVR-first is lifted for no board). No code, build
> file, board id or board profile changes with this ADR, and the port stays
> behind the AVR dev-complete gate (#7). It decides nothing the owner has not
> ruled: every choice is an open question below.

## The question

The nRF54L15-DK has arrived on the bench host. Before any port, the project needs a
record of:

1. what the board is for here, and which SPEC row it serves;
2. its root of trust, and how that maps onto TCG DICE;
3. where it sits against ADR #14 and ADR #15;
4. how it is brought onto the bench without changing its state;
5. what the owner has to decide before a port starts.

The SPEC row `nrf54l15` names `docs/multi-arch/` as its folder, so this ADR
lives here.

## Documents cited

- **[PS]** *nRF54L15 | nRF54L10 | nRF54L05 Datasheet*, Nordic Semiconductor
  (docs.nordicsemi.com, `ps_nrf54l15`). It could not be read in this lane:
  revision **to confirm**, and every section below is cited by the title the
  board profile already records, with its number **to confirm**.
- **[UG]** *nRF54L15 DK Hardware*, Nordic Semiconductor user guide (board
  PCA10156; document version **to confirm**), as cited in
  [`../boards/nrf54l15-dk.md`](../boards/nrf54l15-dk.md).
- **[DICE-HW]** *TCG Hardware Requirements for a Device Identifier Composition
  Engine*, Revision 78, listed (title and URL, no copy) in
  [`../reference/README.md`](../reference/README.md) "Not redistributable";
  layer 0 CDI = HMAC(UDS, H(code)), form (2) of §5, as the AVR tiers implement it.
- **[DICE-AA]** *TCG DICE Attestation Architecture*, Revision 23, listed there
  too; `tcg-dice-UCCS-evidence` (2.23.133.5.4.6, §6.1.4).
- **[nrfutil]** Nordic's `nrfutil device` command, the vendor's own tool for
  the record (version **to confirm** on the bench).

## 1. What the board is for

The SPEC row `nrf54l15` (future): the firmware core as a HAL port on the
nRF54L15's Arm Cortex-M33, with DICE evidence on the part's KMU and TrustZone,
enrolled through go-xkms into go-trusted-platform. It is a Cortex-M33 board
under the `cortex-m` row's layout (ADR #14 decision 2: one `cortex-m`
architecture, the SoC as data, a `src/hal/cortex-m/<soc>/` directory and a
`boards/<board>/profile.mk` with `CPU := cortex-m33`). The board id, SoC
directory name and board directory name are not allocated here.

Three facts from the board profile bear on what it can serve:

- **No Ethernet** ([UG], introduction). The AVR dev-complete transport is
  Ethernet; this board's link to the host is the J-Link OB virtual serial port,
  or a radio. Enrollment's transport is open question 6.
- **RRAM, not flash** ([PS], "NVM — Non-volatile memory"). The boot HAL's
  geometry (pages, write granularity, no erase cycle) differs from AVR's flash.
- **No upstream TF-M port** at v2.3.1 (ADR #15, "Platforms upstream at v2.3.1");
  Nordic carries TF-M for the part in its own SDK.

## 2. Root of trust, and the map onto TCG DICE

### What the part provides

From [PS], by section title; register detail and section numbers **to confirm**.

| Facility | What [PS] says | Section |
|---|---|---|
| Immutable boot | an immutable boot region in RRAM. Whether the ROM or that region verifies a signature over the next stage, and against which key, is **to confirm**; the section read does not call it "secure boot" | "Immutable boot region" |
| KMU | key management unit: keys in a dedicated RRAM region; the KMU and its slots in SICR are accessible from secure mode only | "KMU — Key management unit" |
| RRAM protection | access protection of RRAM regions, and APPROTECT/SECUREAPPROTECT for the debug port. Region granularity and lock semantics **to confirm** | "NVM — Non-volatile memory"; the access port protection section, title **to confirm** |
| TrustZone-M | Armv8-M Security Extension on the Cortex-M33; peripherals assignable to either world | "TrustZone security"; "Peripherals with split security access" |
| CRACEN | crypto accelerator; a KMU key can be pushed to CRACEN so that secure-world code never sees it; DPA countermeasures. Whether it does ECDSA P-256 (needed: owner m522, P-256 on every row) is **to confirm** | "CRACEN — Cryptographic accelerator engine" |
| Tamper | TAMPC and voltage glitch detectors | "TAMPC — Tamper controller"; "GLITCHDET — Voltage glitch detectors" |

### The map onto [DICE-HW] and [DICE-AA]

[DICE-HW] asks for three things: a Unique Device Secret (UDS) readable only by
the DICE, a DICE that runs first and unconditionally after reset and measures
the next layer, and the UDS made unreadable before control leaves the DICE. The
candidate mapping below shows which facility could carry each; none is chosen.

| DICE element | AVR today | nRF54L15 candidate(s) |
|---|---|---|
| UDS storage | soft tier: platform block written by tfw-boot; element tier: ATECC608 | a KMU slot (secure-only, pushable to CRACEN), or a protected RRAM region; open question 2 |
| DICE engine (layer 0) | tfw-boot | code in the immutable boot region (ours, or Nordic's), or a secure-world image after it; open question 3 |
| UDS lock before handoff | tfw-boot zeroes it, BOOTRST/lock bits | a KMU or RRAM protection lock that holds until reset; mechanism **to confirm** in [PS] |
| CDI = HMAC(UDS, H(code)) | `boot/tier/cdi_soft.c`, `cdi_atecc.c` | HMAC-SHA256 on CRACEN with a KMU key, or in software; open question 2 |
| DeviceID key | element tier: hardware-resident, not CDI-derived; soft tier: CDI-derived | KMU-held P-256 key used through CRACEN, or CDI-derived; open question 4 |
| Evidence | `ATTEST` frame, alias `TBSCertificate` with `tcg-dice-UCCS-evidence` ([DICE-AA] §6.1.4), PKCS#10 | unchanged: the three fixed templates (ADR #15, "What we sign today") |

The wire protocol, its vectors and the evidence formats do not change for this
board; any new crypto tier value extends past `TFW_CRYPTO_TPM2` without
renumbering (ADR #15 open question 4).

## 3. Against ADR #14 and ADR #15

Both are PROPOSED; this ADR neither accepts nor amends them.

- **ADR #14** names the i.MX 95 Cortex-M7 as the reference part and the STM32H5
  as the second, Cortex-M33, target. The nRF54L15 is a further M33 board: it
  reuses `src/hal/cortex-m/` and adds a SoC directory (decision 2). QEMU has no
  nRF54L15 machine; the M33 CI stand-in in #14 is `mps2-an505`, whose landing is
  #14 open question 9. Whether this board is ordered before or after the STM32H5
  is open question 1.
- **ADR #15** recommends (b1), TF-M Profile Medium with our attestation code as
  an Application RoT partition, for the M33. It names this part directly: with
  no upstream port, its secure world is Nordic's TF-M or none, which is #15 open
  question 2 (licence check of Nordic's non-secure interface against
  AGPL-3.0-or-later). (a), our own secure world, is #15's reserve for an M33
  part TF-M has no port for, which upstream TF-M is for this part. The KMU plus
  CRACEN claim (a key hidden from secure code) bears on #15 open question 4.

## 4. Onboarding on the bench host

Owner ruling m346/m347 (2026-10-16): every dev board on the bench host is ours to read
and write, on one condition, a factory record that can restore the board. This
ADR's onboarding is the record only.

1. **Cabling** goes to the owner through the CA: the DK's USB (J-Link OB, also
   its power) to the bench host. No lane cables or recables.
2. **One device per lane.** The lane that holds the DK holds no other board,
   and the container gets the J-Link's serial and USB device explicitly.
3. **The factory record, read-out only**, with [nrfutil]: list the device,
   read its device information and protection state, and read out RRAM and
   UICR to a file. Exact subcommands and options are **to confirm** against the
   installed `nrfutil device` before the lane runs. The record is kept on the
   bench host, outside the repository.
4. **Never** an APPROTECT or SECUREAPPROTECT write, a KMU or UICR write, an
   erase or a `recover` (which erases the part). A restore needs a write; how
   it is done without an erase, and who authorises it, is open question 7.
5. **Nothing from the bench enters the repository**: no board serials, J-Link
   serials, MACs, IPs or host names, in docs, logs, tests or commit messages.

## Open questions for the owner

None is decided here.

1. **Order.** Does the nRF54L15 port follow the STM32H5 as the second M33, or
   take its place? Both stay behind the AVR dev-complete gate (#7).
2. **UDS and CDI.** UDS in a KMU slot or in protected RRAM; CDI computed on
   CRACEN with the KMU key, or in software as the AVR soft tier does?
3. **Layer 0.** Our tfw-boot in the immutable boot region, Nordic's boot chain
   (its immutable bootloader and MCUboot) with our layer 0 after it, or TF-M's
   BL2 as #15 (b1) has it? This decides who measures first, which [DICE-HW]
   requires to be unconditional.
4. **DeviceID key.** KMU-held and not CDI-derived (the element-tier precedent),
   or CDI-derived? And a crypto tier value for it (ADR #15 open question 4).
5. **Secure world.** Nordic's TF-M or none (ADR #15 open question 2, with its
   licence check).
6. **Transport.** The board has no Ethernet. Serial over the J-Link OB, a radio,
   or an Ethernet add-on, for enrollment through go-xkms?
7. **Restore.** The factory record is read-out only. Which `nrfutil device`
   write restores it without an erase, and does each restore need the owner's
   yes?
8. **Board id.** One from the Cortex-M range `0x40`-`0x7F`
   ([`../boards/board-id-registry.md`](../boards/board-id-registry.md)),
   allocated in the change that adds the board, as #14 open question 1 has it?
9. **The DICE text.** [DICE-HW] and [DICE-AA] are TCG specifications and are
   not in `docs/reference/`; only their titles, revisions and URLs are. The
   mapping above rests on the sections the AVR code already cites. The KMU and
   RRAM lock sections of [PS] also need a revision pinned before a port relies
   on them.
