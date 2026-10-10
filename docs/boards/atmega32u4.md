# ATmega32U4

> **Status: INFORMATIVE** · Scope: `boards/atmega32u4/` · Last verified: 2026-10-09
>
> The record of what this board does not carry and why. The normative contract
> is [`adding-a-board.md`](adding-a-board.md); the decisions themselves live in
> `boards/atmega32u4/board_profile.h`, where the build reads them.

The 32U4 loses 4 096 bytes of its 32 768 to a DFU bootloader (Caterina) that
cannot be reclaimed, so its budget is half of 28 672: 14 336 bytes of flash.
`make size-check` holds it there.

## Attestation: the evidence frame only

| Command | On this board | Why |
|---|---|---|
| `ATTEST` 0x60 | yes | the evidence frame, with the measurement and the nonce |
| `ATTEST_CERT` 0x63 | **no** | the certificate builder, its template and the claims-set encoder are 2 540 bytes; an element-backed image with them measured 14 706, 370 over budget |
| `ATTEST_CSR` 0x64 | **no** | owner ruling, 2026-10-09 (issue #30); it is built only beside `ATTEST_CERT` in any case |

Both absences answer `TFW_ERR_UNKNOWN_CMD`, and `CAPS` reports feature bits 13
(`ALIAS_CERT`) and 15 (`ATTEST_CSR`) clear, so a host learns this before it
depends on either.

**What is lost.** A 32U4 cannot answer ACME `device-attest-01` (no alias
certificate) and cannot prove possession of its DeviceID key at ACME finalize
(no `ATTEST_CSR`), so it cannot be enrolled through that flow at all.
go-trusted-platform refuses it on the clear `CAPS` bits. `ATTEST` still works,
for runtime freshness.

**Why exclude rather than squeeze.** Enrolment here would need roughly
2.75–2.9 KB: the certificate builder plus the CSR. Dropping 1-Wire, the bus
commands and PWM together still falls short, and would break the promise that
every Arduino capability is on the wire. Raising the budget spends the half the
gate keeps in reserve on purpose. The alternatives, and the zero-byte custom
challenge rejected for every other board, are in
[`../dice-attestation/adr-possession-at-acme-finalize.md`](../dice-attestation/adr-possession-at-acme-finalize.md).
The profile turns `ATTEST_CSR` off by name, not only through `ALIAS_CERT`, so
the exclusion survives the certificate builder ever fitting.
