# OTA — engineering

> **Status: DRAFT** · Issue: #75, #77, #52, #48 · Scope: trusted-firmware · Last verified: 2026-10-22
> Covers the code on main. The design, the state table and the measured sizes
> are in [architecture.md](architecture.md); the decisions in the
> [ADR](adr-ota-image-format-and-trust.md).

## Requirements (the row on main)

Audited 2026-11-16 (#479) against SPEC row `ota` (`docs/SPEC.md`), one line per
requirement of the row's text. A C test is `file: function`, a Go test
`file: TestName`; a make target stands in where the requirement is about what
CI runs. "none" means this audit found no test that proves it.

| # | Requirement | Status | Code | Test |
|---|---|---|---|---|
| 1 | Signed update of the whole image, firmware and application together | done | `tools/image-seal/`, `boot/boot_image.c` | `test/unit/test_image_seal.c: test_a_sealed_region_boots_under_tfw_boot` |
| 2 | Over serial | done | `src/core/ota_stage.c`, `boot/boot_stage.c` | `sdk/go/cmd/tfwctl/ota_stage_simavr_test.go: TestOTAStageThroughTfwBoot` |
| 3 | Over TCP through the W5100 | done | `src/proto/link_eth.c`, `src/core/ota_stage.c` | `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestEthernetStagesAnImage`; `make integration-test-boot` (W5100 model) |
| 4 | Over TCP through the W5500 | partial | `src/proto/link_eth.c` | none: bench only (slice 14) |
| 5 | One update path per image, chosen by object list, never `#if` (item 20) | done | `mk/firmware.mk`, `mk/boot.mk` (`make bootloader` refuses `BOOT_OTA=1 BOOT_STORE=1` on the 2560) | `scripts/test_boot_onepath.py: BootOnePath.test_both_paths_are_refused_with_the_two_choices` |
| 6 | AVR: the application stages into the external SPI store; tfw-boot verifies and copies | done | `src/core/ota_stage.c`, `boot/boot_stage.c` | `test/unit/boot/test_boot_stage.c: test_a_valid_image_copies_and_enters_its_trial` |
| 7 | ATmega2560: A/B slots by swap-move | done | `boot/boot_swap.c` | `test/unit/boot/test_boot_swap.c: test_an_image_refused_in_the_secondary_is_never_swapped`; `test/unit/boot/test_boot_swap.c: test_a_cut_at_any_page_resumes` |
| 8 | ATmega328P: one internal slot, the backup in the store (row limit: no restore, owner queue 176 (a)) | partial | `boot/boot_swap_none.c`, `boards/atmega328p/profile.mk` (`BOOT_PREV`) | none: the store row overflows its boot section by 1498 B and is not in `BOOT_SUPPORTED` (slice 1) |
| 9 | ATmega32U4: one internal slot with the restore from the store | not started | `boot/boot_prev.c` (written, on no supported row) | none on a supported row (slice 2) |
| 10 | ATtiny85: no OTA (ruling 109, 2026-10-26) | done | nothing linked | none: a decision |
| 11 | Cortex-M33: MCUboot, TF-M and PSA Firmware Update composed | not started | none | none (slice 9) |
| 12 | Generic Cortex-M port: the same swap-move as boot-HAL data | done | `boot/boot_swap.c`, `boot/hal/cortex-m/boot_slots.h` | `sdk/go/cmd/tfwctl/ota_ab_qemu_test.go: TestQEMUABRollback`; `sdk/go/cmd/tfwctl/ota_ab_qemu_test.go: TestQEMUABFloor` |
| 13 | ESP32-P4: ESP-IDF's OTA composed (#48) | done | `src/hal/esp32/hal_ota.c` | `test/unit/test_esp_ota.c: test_info_reports_the_running_slot_and_the_session`; `test/unit/test_esp_ota.c: test_auth_activate_and_confirm_are_on_the_wire` |
| 14 | i.MX 95 M7: AHAB composed (#15) | not started | none | none (slice 10) |
| 15 | Trailer v1 with the board id; tfw-boot refuses another board's image | done | `boot/boot_image.c`, `boot/boot_policy.c` | `test/unit/boot/test_boot_policy.c: test_a_signed_image_for_another_board_is_refused`; `test/unit/boot/test_boot_policy.c: test_a_relabelled_board_id_is_refused` |
| 16 | ESP32-P4: the trailer inside the signed image (item 23) | done | `boot/hal/esp32/boot_l0.c` | `test/unit/boot/test_boot_l0_esp32.c: test_another_boards_image_is_refused` |
| 17 | Composed platforms: the trailer beside the vendor's signature | not started | none | none (slices 9, 10) |
| 18 | SUIT-subset manifest (ES256, SHA-256) checked before activation: ATmega2560 element tier, in the application at `OTA_ACTIVATE` (ruling 121) | done | `src/core/ota_manifest.c`, `src/core/suit.c` (`tfw_suit_gate`) | `test/unit/test_ota_manifest.c: test_a_good_manifest_activates`; `test/unit/test_ota_manifest.c: test_another_boards_class_id_is_refused`; `sdk/go/cmd/tfwctl/e2e_ota_manifest_test.go: TestE2EOTAManifestAcceptsAnImageItNames` |
| 19 | The same, ESP32 layer 0 | done | `boot/hal/esp32/boot_l0.c` | `test/unit/boot/test_boot_l0_esp32.c: test_a_manifest_sequence_below_the_floor_is_refused` |
| 20 | The same, soft tier | not started | none: no stored key (owner question 5) | none (slice 4) |
| 21 | The 4 KB parts (328P, 32U4) trailer-only; `make` refuses `OTA_MANIFEST=1` there | done | `mk/firmware.mk`, `src/core/ota_manifest_none.c` | `scripts/test_manifest_refused.py` |
| 22 | The envelope travels after the image, a u16 length first, counted in `OTA_BEGIN`'s total | done | `tools/image-seal/` | `test/unit/test_image_seal.c: test_the_envelope_follows_the_image_with_its_length`; `test/unit/test_image_seal.c: test_an_envelope_counts_in_the_stream_length` |
| 23 | A sequence below the rollback floor refused at `OTA_ACTIVATE` | done | `src/core/ota_manifest.c` | `test/unit/test_ota_manifest.c: test_a_sequence_below_the_floor_is_refused` |
| 24 | tfw-boot checks the `OTA_ACTIVATE` authorization at reset (#137) | done | `boot/boot_cose.c`, `boot/boot_stage.c`, `boot/boot_trial.c` | `test/unit/boot/test_boot_stage.c: test_an_authorized_image_is_taken_over_the_sig_structure`; `test/unit/boot/test_boot_stage.c: test_a_missing_authorization_clears_the_mark` |
| 25 | ATmega2560: tfw-boot checks `OTA_CONFIRM` from the request at EEPROM `0x0EA` (#231) | done | `boot/boot_request.c`, `boot/boot_trial.c` | `test/unit/boot/test_boot_request.c: test_take_honours_a_request_and_clears_it`; `test/unit/boot/boot_main/test_boot_main.c: test_a_confirmed_trial_settles_its_version_and_checks_the_new_floor` |
| 26 | The authorization: COSE_Sign1 ES256 over a detached payload naming action, measurement and version, on every tier | partial: the ESP32's `OTA_CONFIRM` ends the trial with `esp_ota_mark_app_valid_cancel_rollback` and never verifies the held signature (`src/hal/esp32/hal_ota.c:364-382`; [api.md](api.md), "ESP32") (slice 6) | `boot/boot_cose.c` | `sdk/go/cmd/tfwctl/ota_activate_sig_test.go: TestActivateSigStructureMatchesTheVector`; `sdk/go/cmd/tfwctl/ota_activate_sig_test.go: TestConfirmSigStructureIsTfwBoots`; `sdk/go/cmd/tfwctl/e2e_ota_round_test.go: TestE2EOTARoundActivateSignedForAnotherMeasurementIsRefused` |
| 27 | `OTA_AUTH` (0x6F) before the command; without it `TFW_ERR_AUTH_REQUIRED` (17) | done | `src/core/ota_auth.c` | `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestFirmwareOTAAuthLetsTheCommandPastTheGate`; `test/unit/test_esp_ota.c: test_confirm_needs_an_ota_auth_just_before` |
| 28 | The element tier keeps its operator session and adds the check | done | `src/core/ota_auth.c` | `test/unit/test_ota_auth_mutate.c: test_the_element_session_and_ota_auth_are_each_needed_to_activate` |
| 29 | One trial boot within the per-board timeout; the 8 s watchdog armed before the jump, re-armed and petted in the application | done | `boot/boot_trial.c`, `src/core/ota_trial.c` | `test/unit/boot/test_boot_main.c: test_a_new_image_boots_on_trial_armed_before_the_watchdog_and_the_jump`; `test/unit/test_ota_trial.c: test_a_trial_boot_rearms_and_pets`; `test/unit/test_ota_trial.c: test_the_deadline_resets_and_not_before` |
| 30 | The control plane confirms after attestation | partial | `src/core/ota_ctl.c` (device side); the control plane is go-trusted-platform's | `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestE2EOTAConfirmsARunningTrial` (device side only) |
| 31 | A failed trial rolls back by itself; recovery is another OTA round | done | `boot/boot_trial.c`, `boot/boot_swap.c` | `test/unit/boot/test_boot_main.c: test_an_unconfirmed_trial_ends_at_the_next_reset_and_never_runs_again`; `test/unit/boot/test_boot_swap.c: test_a_failed_trial_swaps_back_and_boots_a`; `test/unit/boot/test_boot_swap.c: test_an_image_refused_after_the_swap_swaps_back` |
| 32 | An anti-rollback floor, advanced at confirm, never lowered | done | `boot/boot_image.c` (`tfw_boot_rollback_permit`), `boot/boot_trial.c` | `test/unit/boot/test_boot_trial.c: test_settle_advances_the_floor_to_the_confirmed_version`; `test/unit/boot/test_boot_trial.c: test_settle_never_lowers_the_floor`; `test/unit/boot/test_boot_main.c: test_an_image_under_the_floor_is_refused` |
| 33 | A status code for the wrong OTA state | done | `include/tfw/status.h` (`TFW_ERR_BAD_STATE`) | `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestE2EOTARefusesAnUnsettledConfirm`; `test/unit/test_esp_ota.c: test_confirm_off_trial_is_bad_state` |
| 34 | `OTA_INFO`, `OTA_ACTIVATE`, `OTA_CONFIRM` are 0x6C to 0x6E, serial only | done | `src/core/ota_ctl.c`, `src/core/ota_activate.c` | `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestFirmwareOTAInfo`; `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestFirmwareOTAActivate`; `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestFirmwareOTAConfirm`; `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestE2EOTAControlIsSerialOnly` |
| 35 | A block that cannot be verified fails closed | done | `boot/boot_stage.c`, `src/core/ota_stage.c` | `test/unit/boot/test_boot_stage.c: test_a_bad_hash_copies_nothing`; `test/unit/boot/test_boot_stage.c: test_a_wrong_board_id_copies_nothing` |
| 36 | The store `MARK` cleared at fallback (item 56) | done | `boot/boot_stage.c` | `test/unit/boot/test_boot_stage.c: test_a_fallback_clears_the_mark_and_the_next_reset_takes_nothing`; `test/unit/boot/test_boot_swap.c: test_a_failed_trial_that_falls_back_forgets_its_mark` |
| 37 | ATmega2560 store and soft rows shed OTA before verification (item 79) | done | `mk/boot.mk` (`BOOT_SUPPORTED` has one soft 2560 row, `atmega2560:soft:soft:p256-soft:0:0`) | `scripts/test_boot_shed_ota.py: BootShedOta.test_ota_and_the_store_are_refused_on_the_soft_tier`; `scripts/test_boot_shed_ota.py: BootShedOta.test_the_default_soft_row_is_the_base_row` |
| 38 | `tfwctl ota-stage` over `(*Device).StageImage`, and `tfwctl ota-status` | done | `sdk/go/ota.go` | `sdk/go/cmd/tfwctl/ota_stage_test.go: TestOTAStageSendsTheImageInChunksAndPrintsTheFinalState`; `sdk/go/cmd/tfwctl/ota_stage_test.go: TestOTAStatusPrintsEachState`; `sdk/go/cmd/tfwctl/e2e_ota_resume_test.go: TestE2EOTAStageResumesAfterALostFrame` |
| 39 | This project and `sdk/go` hold no private key; the host sends the signature it is given | done | `sdk/go/ota.go` | `sdk/go/cmd/tfwctl/ota_stage_test.go: TestOTAActivateAndConfirmSendTheAuthorizationFirst` |
| 40 | `make image-tbs` and `make image-seal` seal an AVR image for its board | done | `tools/image-seal/` | `test/unit/test_image_seal.c: test_the_span_is_the_wire_vector_for_this_board` |
| 41 | `tfwsim --boot-key` runs the real tfw-boot through the round (#52) | done | `boot/boot_trial.c`, `boot/boot_stage.c` | `sdk/go/cmd/tfwctl/e2e_ota_round_test.go: TestE2EOTARoundBootsTheActivatedImageOnTrial`; `sdk/go/cmd/tfwctl/e2e_ota_round_test.go: TestE2EOTARoundUnconfirmedTrialEndsAtTheNextReset`; `sdk/go/cmd/tfwctl/e2e_ota_round_test.go: TestE2EOTARoundImageBelowTheFloorIsNotTaken` |
| 42 | A TCP session's authorization ends with its connection | done | `src/proto/link_eth.c`, `src/core/ota_auth.c` | `test/unit/test_ota_hold_link.c: test_an_ota_auth_over_ethernet_holds_nothing_past_its_connection`; `sdk/go/cmd/tfwctl/e2e_ota_test.go: TestE2EOTAAuthSessionEndsWithItsConnection` |
| 43 | `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP | not started | `src/proto/link_eth.c` keeps them off the allow-list (owner item 48) | `test/unit/test_link_eth.c: test_ethernet_admits_the_enrolment_attestation_and_staging_commands_and_nothing_else` pins today's refusal (slice 5) |
| 44 | ESP32: layer 0 verifies the activate authorization at the trial boot and boots the previous slot when refused | done | `boot/hal/esp32/boot_l0.c` | `test/unit/boot/test_boot_l0_esp32_mutate.c: test_a_trial_not_authorized_falls_back`; `test/unit/boot/test_boot_l0_esp32_mutate.c: test_a_trial_slot_with_its_authorization_is_loaded` |
| 45 | ESP32: an unconfirmed trial rolls back at 60 s | done | `src/hal/esp32/hal_ota.c` | `test/unit/test_esp_ota.c: test_a_confirm_before_the_deadline_keeps_the_image`; `test/unit/test_esp_ota.c: test_no_confirm_at_the_deadline_resets_into_the_rollback` |
| 46 | ESP32: ESP-IDF's anti-rollback; which releases advance the eFuse floor | partial | esp32c3 QEMU only; the release policy is the owner's (question 9) | `make esp32-qemu-test` (slice 11) |
| 47 | Every board reflashed with an image that carries its board id | partial | the 2560 and the ESP32-P4 done; the Nano not | none: bench records (slice 12) |
| 48 | Seams: go-trusted-platform `device-ota`, go-quicbaas reference values, qrdb `ota.activate`/`ota.confirm`, go-xkms signer and provisioning row | not started | other projects ([README.md](README.md), "Seams") | none in this repository (slice 13) |

### Remaining slices

1. ATmega328P store row: make it fit its 4 KB boot section or take the owner's ruling (#52); touches `boot/`, `boards/atmega328p/profile.mk`; proved by `make bootloader-check`; owner decision.
2. ATmega32U4 restore from `PREV` on a supported row and under simavr; touches `boot/boot_prev.c`, `test/integration/simavr/boot_harness_prev.c`; proved by `make integration-test-boot`; needs the core to shrink by 1 724 B or an owner ruling.
3. ATmega32U4 `ETH=1` stager measured against its 14 336 B budget; touches `boards/atmega32u4/profile.mk`; proved by the coordinator's size check.
4. Manifest check on the soft tier with a key home; touches `src/core/ota_manifest.c`, `mk/firmware.mk`; proved by a soft-row case in `test/unit/test_ota_manifest.c`; owner question 5 first.
5. `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP; touches `src/proto/link_eth.c`; proved by `test/unit/test_link_eth.c` and `TestE2EOTAControlIsSerialOnly` turned to admit them; owner item 48 first.
6. ESP32 confirm verifies the signature: `OTA_CONFIRM` checks the held COSE_Sign1 confirm signature over the running image's measurement and version with the image key before it calls `esp_ota_mark_app_valid_cancel_rollback`; touches `src/hal/esp32/hal_ota.c`; proved by a case in `test/unit/test_esp_ota.c` that an invalid signature answers a refusal, leaves the trial running and never calls the mock's `esp_ota_mark_app_valid_cancel_rollback`; no hardware.
7. Done (#618): rows 28 and 37 name their tests; `scripts/test_boot_shed_ota.py` pins item 79's shed-before-verification on the 2560.
8. Done (#618): row 45 names the deadline cases in `test/unit/test_esp_ota.c`.
9. Cortex-M33 under TF-M: `0x68`–`0x6E` mapped onto PSA Firmware Update, the trailer beside MCUboot's signature; owner question 8 first.
10. i.MX 95 M7 under AHAB, the trailer beside the container signature; with #15 and trusted-imx95.
11. ESP32-P4 eFuse anti-rollback on the board; hardware, and owner question 9.
12. Bench reflash of every board with a board-id image (the Nano waits on the ISP cabling question); hardware.
13. The seams in go-trusted-platform, go-quicbaas, qrdb and go-xkms, and `tfwprov` placing go-xkms's key once its row lands; other projects.
14. The W5500 on a real network; hardware.

### Open question: `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP

Nothing is built for this until a ruling admits it.

- **What the allow-list says.** `tfw_link_admits` (`src/proto/link_eth.c`)
  admits, over TCP, `CAPS`, `APP_INFO`, the three attestation reads and the four
  staging ids `OTA_BEGIN` to `OTA_STATUS` (0x68 to 0x6B). The ADR's decision 8
  narrowed it this way (owner item 48 on #55). `OTA_AUTH` (0x61, 0x62),
  `OTA_ACTIVATE` (0x6D) and `OTA_CONFIRM` (0x6E) are serial only: over TCP they
  answer `TFW_ERR_UNKNOWN_CMD`, which `TestE2EOTAControlIsSerialOnly` pins. The
  harness's network leg stages over TCP and activates over the UART
  (`net_activate`).
- **What admitting them would change.**
  - `tfw_link_admits` gains three cases: `OTA_AUTH`, `OTA_ACTIVATE` and
    `OTA_CONFIRM`. Either both `OTA_AUTH` ids are admitted, or the authorization
    travels inside the activate and confirm frames, which is a wire change.
  - A TCP session's authorization ends when its connection closes (the ADR's
    open question 8, settled under ruling 109 on 2026-10-26). Otherwise a token
    taken on one connection could be spent on another. No auth state opens on
    a TCP connection today, so a close has nothing to clear;
    `test/unit/test_ota_hold_link.c` and
    `TestE2EOTAAuthSessionEndsWithItsConnection` pin that.
  - `TestE2EOTAControlIsSerialOnly` flips, and the harness's network leg could
    activate over TCP instead of the UART. New cases are needed: an activate
    over TCP refused without a session, and a session lost at connection
    close.
  - No command id and no status code changes. tfw-boot gains no byte, because
    it reads the same request record whichever link wrote it.
  - The ADR's requirement that every board update over the network (it names
    the 328P) is then met over Ethernet with no serial step.
- **Recommendation.** Admit all three, with a session bound to its connection
  (open question 8, now settled). Until then, a fleet update over
  Ethernet still needs a serial step on every device, and the network leg
  exists only to stage.

### What the simavr harness lacks for the 328P and 32U4

`boot_harness.c`'s `prev` mode is written: an unconfirmed trial restores `PREV`,
a refused pending image restores it, and a corrupt `PREV` halts. The harness now
takes from the board profile it is built with what used to be fixed to the 2560:

- **The run.** `make integration-test-boot-prev BOARD=<board>` runs the `prev`
  row for the board named. It refuses, before building, a board with no `PREV`
  in a store (`BOOT_AB=1`, no `OTA_STORE`, or `BOOT_PREV=0`, which is the 328P's
  by owner queue 176, option (a): no boot-time rollback there). It is not in `ci`.
  `scripts/test_boot_prev_target.py` pins it.
- **The store's chip select.** `attach_store` listens on `TFW_OTA_STORE_CS_IO`
  (`boot_harness_io.h`): PH5 on the 2560, PB0 on the 328P, PB4 on the 32U4.
- **The UART.** The harness listens on `TFW_UART_N` and paces on that USART's
  `UCSRnA`: USART0 at 0xC0 on the 2560 and the 328P, USART1 at 0xC8 on the 32U4.

`test/unit/boot/test_boot_harness_io.c`, built under each of the three profiles,
pins the select and the UART.

One thing is left, on the 32U4 (the 328P keeps no restore, so its row has no
`prev` run; its store row, without the restore, is not in `BOOT_SUPPORTED`:
at `cb104cf4`, `make bootloader-check` (avr-gcc 14.2.0) found it overflows its
4096-byte boot section (the part's largest `BOOTSZ`) by 1498 bytes, while the
base row fits at 3712 / 4096; activation on the 328P goes back to the owner
(#52)):

1. **No tfw-boot to load.** The 32U4's `BOOT_STORE=1` row is not in
   `BOOT_SUPPORTED`, because its boot image does not fit: it is 1 724 bytes
   over the 4 096-byte section, measured with `make bootloader-candidate` on
   the CI image, avr-gcc 14.2.0, at `9a338a82` ([architecture.md](architecture.md)).
   The row stays out of `BOOT_SUPPORTED`, and `integration-test-boot-prev`
   stays stopped at the boot budget gate. The CI image measures a
   candidate row, without adding it, with

   ```sh
   make bootloader-candidate BOOT_CANDIDATE=atmega32u4:atecc608a:atecc608a:atecc608a:0:1
   ```

   It prints the image against the 4 096-byte section, or ld's overflow and
   `boot-core-size`, and exits 0 only when the row fits.
   `scripts/test_boot_candidate_target.py` pins its arguments. A row that fits
   joins `BOOT_SUPPORTED` once `make bootloader-check` passes it; one that does
   not is recorded with its number and stays out. Until then
   `integration-test-boot-prev` stops at the boot budget gate.

The restore logic itself is covered on the host by
`test/unit/boot/test_boot_prev.c` (328P profile).

### The split gate, measured per row (ruling 121)

Owner ruling 121 (decision 106, option (b), CA m957, 2026-10-21) lets the
application check the manifest fields at `OTA_ACTIVATE` on a row where boot
cannot fit them. tfw-boot keeps the P-256 verify over image and trailer on every
row, and the soft row keeps the full gate in boot. The ruling asks for each
row's byte split before any code is written. This is that measurement.

**What "the field checks" are on AVR.** tfw-boot on AVR parses no SUIT
envelope; the application's manifest reader does (`src/core/suit.c`). The field
checks in boot are the trailer's own fields, in `boot/boot_image.c`:

- envelope: magic and layout version, in `tfw_boot_trailer_parse`;
- sequence: `image_version` not 0, in `tfw_boot_trailer_parse`;
- board id and image flags: `tfw_boot_binding_permit`;
- anti-rollback: `tfw_boot_rollback_permit` against the floor.

They run in `tfw_boot_evaluate_at` (`boot/boot_policy.c`) on every row, and again
in `boot/boot_stage.c` on the `BOOT_STORE=1` rows. The `image_len` bound in
`tfw_boot_trailer_parse` is not a field check: the measurement and the verify
depend on it, so it stays in boot on every row. Each check is a byte or
32-bit compare and a branch. With LTO they are inlined into their callers, so
the linked ELF has no symbol of their own.

**How each number was got.** "rec." is a figure recorded in the tree or
measured, with its source. The field-check column is measured at `48d85486`
with avr-gcc 14.2.0: the sum of the `avr-nm --size-sort -S` sizes of
`tfw_boot_binding_permit` (30), `tfw_boot_rollback_permit` (32) and
`tfw_boot_trailer_parse` (238; 242 on the soft row) from each row's
`make bootloader-budget` build, the command after the table. That is 300 B per
call site (304 on the soft row), times the number of call sites. It is an upper
bound: `tfw_boot_trailer_parse` also holds the `image_len` bound, which stays
in boot, and inlining removes the call overhead. The verify-only and headroom
columns are the full-gate figure less that bound, so they are bounds too. The
application column is still an estimate, "est.": `bootloader-budget` does not
measure the application.

| Row (`BOARD:TIER:MEASURE:SIG:OTA:STORE`) | Boot, full gate | Field checks in boot | Boot, verify only | Headroom vs BOOTSZ, verify only | Application: field checks at `OTA_ACTIVATE`, headroom vs `TFW_FLASH_BUDGET` | Gate |
|---|---|---|---|---|---|---|
| `atmega328p:atecc608a:atecc608a:atecc608a:0:0` | 3 746 / 4 096, rec. (`mk/boot.mk`, avr-gcc 14.2.0); `architecture.md` records 3 924 | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0), one call site | ≥ 3 446 | ≤ 650 | no OTA in boot, nothing to stage: n/a | full, fits |
| `atmega32u4:atecc608a:atecc608a:atecc608a:0:0` | 3 816 / 4 096, rec. (`mk/boot.mk`) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 3 516 | ≤ 580 | n/a, as the 328P | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:0:0` | 3 906 / 8 192, rec. (`mk/boot.mk`) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 3 606 | ≤ 4 586 | n/a | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:1:0` | 7 954 / 8 192, rec. (`architecture.md`, lane-verify, with the confirm request) | ≤ 300, rec. (`48d85486`, avr-gcc 14.2.0) | ≥ 7 654 | ≤ 538 | ≤ 100, est.; app about 24 445 / 65 536 rec. (`architecture.md`) | full, fits |
| `atmega2560:atecc608a:atecc608a:atecc608a:0:1` | 6 492 / 8 192, rec. (`architecture.md`, lane-verify) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0), two call sites | ≥ 5 892 | ≤ 2 300 | ≤ 100, est.; app as above | full, fits |
| `atmega2560:soft:soft:p256-soft:0:0` | 8 064 / 8 192, rec. (`mk/boot.mk`) | ≤ 304, rec. (`48d85486`, avr-gcc 14.2.0) | not built: the soft row keeps the full gate | 128 with the full gate, rec. | n/a | full, by ruling 121 |
| `mps2-an505:soft:soft:p256-soft:0:0` | 6 956 / 32 768, rec. (`make bootloader-check` at 48d85486, arm-none-eabi-gcc 14.2.1); ram 324 / 32 768 | not measured | not built: soft row | 25 812 with the full gate, rec. | n/a | full, by ruling 121 |
| `atmega328p:atecc608a:atecc608a:atecc608a:0:1` (with the `PREV` restore, now dropped: the row links no restore, is not in `BOOT_SUPPORTED`, and measured 1498 bytes over without it) | 5 814 / 4 096: 1 718 over, rec. (`make bootloader-candidate` on the CI image, slice 6) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0, the `-noprev` build), two call sites | ≥ 5 214 | ≥ 1 118 over | ≤ 100, est.; app about 19 267 with `ETH=1` and the stager, already over the 16 384 policy line, rec. (`architecture.md`) | does not fit either way |
| `atmega32u4:atecc608a:atecc608a:atecc608a:0:1` (candidate) | 5 804 / 4 096: 1 708 over, rec. (as above) | ≤ 600, rec. (`48d85486`, avr-gcc 14.2.0), two call sites | ≥ 5 204 | ≥ 1 108 over | ≤ 100, est.; app not measured (`ETH=1` builds with the W5100 `profile.mk` names; not measured), budget 14 336 | does not fit either way; not in `BOOT_SUPPORTED` |

**The result.** No row needs the split:

- Every `BOOT_SUPPORTED` row already fits with the full gate.
- The two rows that do not fit, the 328P and 32U4 store rows with the restore,
  are 1 718 and 1 708 bytes over (the 32U4's, 1 724 at `9a338a82`; the 328P's restore is dropped since, owner
  queue 176, option (a)), and the 328P store row without it measured 1498 bytes
  over at `cb104cf4`. Moving the field checks out recovers 600 bytes at
  most, measured (two call sites of 300), so both stay more than 1 100 bytes
  over, and the 328P store row without the restore about 900.

So no row splits, and no code was written for the split. Ruling 121's split
gives the 4 KB parts about a third of what they lack, at most. Restoring from
`PREV` on those parts needs a different answer, and that is the owner's to give.

A split would also leave boot unable to refuse a downgrade on its own. Without
the floor check in boot, an image that reached `MARK` without going through
`OTA_ACTIVATE`, written by the application to the store, is installed whatever
its version, provided its signature verifies. If a later row ever needs the
split, that trade-off goes to the owner along with the numbers.

**Exact numbers, on the CI image.** For each row, set its six fields and run:

```sh
make bootloader-budget BOARD=<b> BOOT_TIER=<t> BOOT_MEASURE=<m> BOOT_SIG=<g> BOOT_OTA=<o> BOOT_STORE=<s>
avr-nm --size-sort -S build/<b>/boot/<t>-<m>-<g>-ota<o>-store<s>/measure/boot_boot_image.c.o
```

`bootloader-budget` compiles `boot/boot_image.c` without LTO and with
`-ffunction-sections`. `avr-nm` then prints `tfw_boot_binding_permit`,
`tfw_boot_rollback_permit` and `tfw_boot_trailer_parse` at their own sizes,
which bound the inlined cost from above. The 328P store row builds into
`ota0-store1-noprev`. The full-gate column comes from
`make bootloader-check`, and for the two candidates from
`make bootloader-candidate BOOT_CANDIDATE=<row>`. The application column can be
measured only once checks exist at `OTA_ACTIVATE`: it is the difference in
`make firmware BOARD=<b> ETH=1` (`size-check`) between the two commits.
Slice s8 put them there on the 2560's element tier (`src/core/ota_manifest.c`),
so the 2560 figure is now `make firmware BOARD=atmega2560 CRYPTO=atecc608a ETH=1`
before and after its feature commit, on the CI image; the "≤ 200, est." stands
until then.

## Files

| File | Role |
|---|---|
| `src/core/ota_stage.c`, `src/core/ota_stage.h` | Application stager: `OTA_BEGIN`, `OTA_DATA`, `OTA_END`, `OTA_STATUS` into the external store's `STAGE` area; checks the trailer, board id and SHA-256 at `OTA_END`, then writes `MARK` |
| `src/core/ota_manifest.c`, `src/core/ota_manifest_none.c` | `OTA_ACTIVATE`'s manifest check (ruling 121): reads the envelope after the staged image and holds it to the staged trailer through `tfw_suit_gate`. The stub, which answers `TFW_OK`, on every stager that does not link the reader |
| `src/core/ota_stage_none.c` | Linked instead where no stager is built: a no-op `tfw_stage_init`, which `main.c` calls on every image |
| `src/hal/esp32/hal_ota.c` | The ESP32's stager and `OTA_INFO` on ESP-IDF's `esp_ota` (#48): the inactive app slot, ESP-IDF's image check at `OTA_END`, nothing that activates ([api.md](api.md), "ESP32") |
| `src/store/spi_nor.c` | The store driver the board's `OTA_STORE` names |
| `src/core/commands.def` | The four `TFW_CMD_ENTRY` rows, under `TFW_FEATURE_OTA_STAGE` |
| `src/proto/link_eth.c` | `tfw_link_admits`: the four ids are on the Ethernet allow-list |
| `boot/boot_stage.c`, `boot/boot_stage_none.c` | tfw-boot reads `MARK`, checks the staged image again and copies it into the destination slot; the stub where `BOOT_STORE=0` |
| `boot/boot_swap.c`, `boot/boot_swap_none.c` | The 2560's A/B swap-move and its EEPROM journal; the stub on every other row |
| `boot/boot_trial.c` | The one trial: `PENDING`, `TRIAL`, `CONFIRMED`, `FAILED`, and the floor at settle |
| `test/model/spi_nor_model.c` | NOR part model on the mock's SPI bus, shared by the unit tests, `tfwsim` and the simavr harness |
| `tools/image-seal/` | The build's sealer for tfw-boot rows, built per board: emits trailer v1's 48-byte signed span (board id from the board's profile) for go-xkms to sign, then wraps the signature into the trailer and writes the stream `tfwctl ota-stage` sends and the region tfw-boot's serial session writes. On a board that checks the manifest the stream carries `make suit-envelope`'s envelope after the image, a u16 length first. Encodes and reads back through `boot/boot_image.c`; holds no key |
| `sdk/go/ota.go` | `OTABegin`, `OTAData`, `OTAEnd`, `OTAStatus`, `StageImage` |

## Gating by object list

No `#if` outside the four allowed places selects any of this.

- **Application** (`mk/firmware.mk`). `OTA_STAGE` defaults to `ETH` where the
  board's `profile.mk` names an `OTA_STORE`, and to `0` otherwise. With
  `OTA_STAGE=1` the build links `ota_stage.c`, the store driver, `sha256.c` and
  `hal_bus.c`, and passes `-DTFW_FEATURE_OTA_STAGE=1`, which only
  `commands.def` reads. Every stager links `ota_activate.c` and, where the
  board links the SUIT reader (`OTA_MANIFEST := 1` on the element tier, the
  2560 today), `ota_manifest.c`; elsewhere `ota_manifest_none.c`.
  Otherwise it links `ota_stage_none.c`. `OTA_STAGE=1` on
  a board with no `OTA_STORE` is a build error. A serial image with the stager
  gets the `-stage` suffix; an Ethernet image without it gets `-nostage`.
  An architecture whose update path is its own names it in `ARCH_OTA_SRCS`
  (`mk/arch/esp32.mk`: `src/hal/esp32/hal_ota.c`). That file is then the
  stager on every image, in place of `ota_stage.c` and the store, with
  `-DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_INFO=1` and no
  `TFW_FEATURE_OTA_ACTIVATE`.
- **Boot** (`mk/boot.mk`). `BOOT_STORE` (default `0`) links `boot_stage.c` or
  `boot_stage_none.c`. `boot_swap.c` is linked where the board has the slots
  (`BOOT_AB` in `profile.mk`) and the row is `BOOT_OTA=1` or `BOOT_STORE=1`.
  `boot_trial.c` is linked on every row. `BOOT_STORE` is the sixth field of a
  `BOOT_SUPPORTED` row ([secure-boot engineering](../secure-boot/engineering.md)),
  and `make bootloader` refuses `BOOT_OTA=1 BOOT_STORE=1` on the 2560.

## Tests

| Target | Covers |
|---|---|
| `make test` (`test_ota_stage`) | The stager against the NOR model: begin, data, end, status, and each refusal |
| `make boot-test` (`test_boot_stage`, `test_boot_stage_2560`, `test_boot_swap`, `test_boot_trial`, `test_boot_main`) | The copy, a cut copy, the swap and every cut point, rollback, confirm, the floor; listed in [architecture.md](architecture.md). `test_boot_main` runs tfw-boot's own `boot/boot_main.c`, its `main` renamed by the link, on the host board's in-place row: arm before the watchdog and the jump, an unconfirmed trial failed at the next reset, a settle, the floor refusals, and a reset before each write |
| `make integration-test-host` (`sdk/go/cmd/tfwctl`, `TestEthernetStagesAnImage`, `TestEthernetStagerRefusesALengthNotWholePages`, `TestE2EOTAStageResumesAfterALostFrame`) | `tfwctl ota-stage` (`Upload`) against `tfwsim` over serial and TCP, and its going on from `OTA_STATUS`'s count after a lost `OTA_DATA`, or on a board with `make hw-test HW_ETH=`; needs `make tfwsim` |
| `make boot-test` (`test_boot_prev`, `test_boot_settle`), `make host-test` (`test_ota_stage`) | The 328P/32U4 fall-back writes `TFW_BOOT_EE_FLAG_REFUSED` after a refusal and clears it after an unconfirmed trial; the flags byte stays inside the block's CRC; `OTA_INFO` reports it as flags bit 1 beside `FAILED` only (no 328P/32U4 image links `OTA_INFO` yet, and the 2560 does not write the bit) |
| `make host-test` (`test_image_seal`) | The sealer against the 2560 tfw-boot with p256-soft: the span is the wire vector's layout, a sealed region boots, the floor and a changed byte refuse it, the stream installs as the region, the largest image boots, and a span or signature for another image is refused; the envelope's length bytes and bytes after the image, as literals, and the page it can add |
| `make host-test` (`test_ota_manifest`) | `OTA_ACTIVATE`'s manifest check on a 2560 element stager, from `testdata/suit_vectors.h`: a good envelope marks; no envelope, a length of zero or past the padding, a flipped signature byte, another board's class id, an image the manifest does not name and a sequence that is not the trailer's version each leave the store unmarked |
| `make test` (`test_esp_ota`) | The ESP32 session against a mock `esp_ota` (`test/mock/esp_ota_mock.c`): every accept path and refusal, `OTA_INFO`, and nothing that activates |
| `make esp32-qemu-test` (`test/integration/esp32/qemu_test.py`) | Over Ethernet in QEMU: a tampered app refused at `OTA_END`, the app written to `ota_1`, `ota_0` and `otadata` untouched, and the next boot still on `ota_0` |
| `make integration-test-boot` | The real tfw-boot and application under simavr on two 2560 rows, `BOOT_OTA=1` and the store row; the cases are in [architecture.md, "The OTA round under simavr"](architecture.md#the-ota-round-under-simavr-75-77) |

The simavr suite needs simavr and avr-gcc. It runs one fixed row per board
(`BOOT_INT_BOARD`, `BOOT_INT_ROW` in `mk/boot.mk`) whatever `BOARD` the caller
set.

## Sizes

The boot sizes come from `make bootloader-check`, which builds every
`BOOT_SUPPORTED` row and checks it against the boot section. The application
sizes come from `make firmware`'s size check. `make bootloader-budget` breaks
tfw-boot down by part. The current numbers are in
[architecture.md](architecture.md) and
[secure-boot/cli.md](../secure-boot/cli.md); this page does not copy them.
