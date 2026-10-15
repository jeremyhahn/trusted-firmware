# Roadmap

> **Status: REFERENCE** · Last verified: 2026-10-14
> What is complete and what remains, in milestone order. Features and requirements: [docs/SPEC.md](docs/SPEC.md).

## Mission

Bare-metal firmware for the trusted platform: one wire protocol, one Go SDK. Devices prove what they are (DICE) and enroll into the platform. AVR first; other architectures only after AVR is complete and proven end to end.

## Milestones

- [Dev complete](../../milestone/35) — Dev complete — no projected date set

## Complete

- **wire-protocol** — wire and persisted constants under the new names, vectors re-pinned — [#3](../../issues/3) — done
- **wire-protocol** — CAPS pin descriptor generalised, `free_ram` widened, board-id registry — [#12](../../issues/12) — done
- **device-capabilities** — architecture-neutral structure, proven on AVR — [#21](../../issues/21) — done
- **device-capabilities** — HAL contract free of AVR types — [#9](../../issues/9) — done
- **device-capabilities** — reset cause abstracted behind the HAL — [#10](../../issues/10) — done
- **device-capabilities** — conditional-confinement and arch-leak lint gates — [#13](../../issues/13) — done
- **boards** — `ARCH` selected by the board profile — [#8](../../issues/8) — done
- **boards** — ATmega328P, ATmega2560, ATmega32U4 and ATtiny85 released — [v0.3.0-alpha](../../releases/tag/v0.3.0-alpha) — done
- **secure-boot** — boot geometry, OTA page size, NVM and session timing from the board — [#11](../../issues/11) — done
- **secure-boot** — `boot.mk` core-size link freed of AVR-only flags — [#25](../../issues/25) — done
- **dice-attestation** — ADR: DeviceID key possession at ACME finalize — [#6](../../issues/6) — done
- **dice-attestation** — `ATTEST_CSR` (0x64), a device-templated PKCS#10 — [#30](../../issues/30) — done
- **device-provisioning** — provisioning commands and `tfwprov` released — [v0.3.0-alpha](../../releases/tag/v0.3.0-alpha) — done
- **host-sdk** — mechanical rename to `tfw_`, `tfwsim`, `tfwctl`, `tfwprov` — [#2](../../issues/2) — done
- **host-sdk** — `Device.AttestCSR` — [#31](../../issues/31) — done
- **host-sdk** — `tfwsim` and host objects rebuild on a header change — [#29](../../issues/29) — done
- **host-sdk** — `sdk/go` released — [sdk/go/v0.3.0-alpha](../../releases/tag/sdk/go/v0.3.0-alpha) — done

## Now

- **ethernet-transport** — the wire protocol over a WIZnet W5100 or W5500 on the Nano and the Mega 2560, enrolling and attesting over Ethernet; closes AVR — [#55](../../issues/55) — open
- **xkms-adapter** — phases B and C: registered through go-xkms's hook, finalize through `Device.AttestCSR`, `xkms/v0.2.0-alpha` — [#28](../../issues/28) — blocked: go-xkms registry hook tag
- **xkms-adapter** — AVR enrollment into go-trusted-platform, through the adapter — [#20](../../issues/20) — open
- **xkms-adapter** — downstream migration to the adapter — [#19](../../issues/19) — open
- **xkms-adapter** — physical-board enrollment run end to end, the gate for other platforms — [#7](../../issues/7) — open

## Next

None queued.

## Later

None planned.

## Future

Gated: no other platform until AVR has every primitive and a proven physical end-to-end run (#7). Design: [docs/multi-arch/architecture.md](docs/multi-arch/architecture.md). Both wait behind the AVR gate, in this order: first AVR proven on the Nano and the Mega ([#7](../../issues/7)), then the SE052F, then the i.MX 95's Cortex-M7 as the cortex-m reference part, then the STM32H5 with the SE052F on its bus as the reference trusted device. The OPTIGA Trust M, RP2350, ESP32-P4 and nRF54L15 come after the SE052F and the STM32H5; there is no order of work among the RP2350, ESP32-P4 and nRF54L15 yet: the owner decides when AVR is proven.

- **secure-element-se05x** — the SE052F element tier — no issue yet — blocked: AVR gate (#7)
- **imx95** — i.MX 95 Cortex-M7 on the FRDM-IMX95-PRO, the cortex-m reference part — [#49](../../issues/49) — blocked: AVR gate (#7)
- **hsm-self-defence** — i.MX 95 M7 enforcement of the tamper response under tenant-signed policies and scripts, with seams to go-xkms and go-trusted-platform; the NPU policy selection only after the HSM node runs — [#50](../../issues/50) — blocked: AVR gate (#7)
- **stm32h5** — STM32H5, the second Cortex-M target, with the SE052F on its bus as the reference trusted device — no issue yet — blocked: AVR gate (#7)
- **optiga-trust-m** — the OPTIGA Trust M element tier — no issue yet — blocked: AVR gate (#7)
- **rp2350** — RP2350 on the Pico 2 W — no issue yet — blocked: AVR gate (#7)
- **esp32-p4** — ESP32-P4 on a PoE Ethernet board, its ESP32-C6 co-processor off — [#48](../../issues/48) — blocked: AVR gate (#7)
- **nrf54l15** — nRF54L15 on the nRF54L15-DK — no issue yet — blocked: AVR gate (#7)
- **mcxn947** — MCXN947 on the FRDM-MCXN947 — no issue yet — blocked: AVR gate (#7)
- **cortex-m** — Phase 5: the Cortex-M architecture ports, Cortex-M7 and Cortex-M33 first — [#22](../../issues/22) — blocked: owner gate (#7)
- **cortex-m** — ADR: Cortex-M reference part and CI emulation target — [#14](../../issues/14) — blocked: owner gate (#7)
- **cortex-m** — ADR: Cortex-M trust model and the relation to TF-M — [#15](../../issues/15) — blocked: owner gate (#7)
- **esp32** — Phase 6: the ESP-IDF architecture port, the ESP32-P4 first — [#23](../../issues/23) — blocked: owner gate (#7)
- **esp32** — ADR: ESP-IDF integration boundary and transport — [#16](../../issues/16) — blocked: owner gate (#7)
- **application-sdk** — the application interface for `trusted-firmware-<application>` repositories — [#51](../../issues/51) — open
- **ota** — signed whole-image updates with rollback, from go-trusted-platform's control plane — [#52](../../issues/52) — open
- **device-onboarding** — DICE-only devices enroll by device profile through the adapter seam — [#53](../../issues/53) — open

## Dependencies

Upstream, bottom-up; each step is released before the next:

1. go-xkms tags the backend registry hook.
2. Here: #28 phases B and C, tagged `xkms/v0.2.0-alpha` — [#28](../../issues/28).
3. go-quicbaas registers the adapter in its composition; go-trusted-platform and xkey reach the devices through go-quicbaas and never import the adapter — [go-trusted-platform#771](../../../go-trusted-platform/issues/771), [go-xkms#1440](../../../go-xkms/issues/1440).
4. go-xkms removes its AVR backend — [go-xkms#1439](../../../go-xkms/issues/1439).
5. go-quicbaas drops its indirect require — [go-quicbaas#1185](../../../go-quicbaas/issues/1185).

- Pinned: go-xkms v0.51.0-alpha in `xkms/go.mod`.
- go-xkms profiles the possession CSR and verifies the DICE evidence — [go-xkms#1441](../../../go-xkms/issues/1441).
- go-trusted-platform enrolls devices — [go-trusted-platform#696](../../../go-trusted-platform/issues/696), [go-trusted-platform#772](../../../go-trusted-platform/issues/772).
- Downstream: go-xkms and go-trusted-platform import `sdk/go`, so it keeps zero in-house dependencies.
