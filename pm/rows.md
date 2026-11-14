# SPEC rows

One SPEC row = one row issue = one branch `row/<issue>-<row>` = one lane session = one PR (handbook `software/conventions/sdlc.md` §2b). Every open issue sits under its row issue or is listed below. Rows are in [docs/SPEC.md](../docs/SPEC.md) order.

| Row | Row issue | Status | Folded issues | Open PRs |
|---|---|---|---|---|
| ethernet-transport | #431 | in progress | 1 | none |
| cortex-m | #161 | in progress | 3 | #416, #415, #412, #406, #384, #381 |
| stm32h5 | #425 | in progress | 0 | #421, #417, #411, #404, #394, #391, #364 |
| rp2350 | #429 | future | 0 | #393, #390, #380, #373, #366 |
| nrf54l15 | #427 | future | 1 | #418, #414, #407, #386, #385 |
| mcxn947 | #426 | in progress | 0 | #420, #419, #413, #408, #399, #392, #375 |
| optee-trustzone | #430 | future | 0 | #370 |
| esp32 | #23 | in progress | 0 | none |
| esp32-p4 | #48 | in progress | 0 | none |
| imx95 | #428 | in progress | 0 | #403, #395, #388 |
| device-security-event-evidence | #283 | planned | 0 | none |
| ota | #52 | in progress | 1 | #400, #396 |
| device-onboarding | #53 | future | 0 | none |

Rows with no open work: `wire-protocol`, `device-capabilities`, `boards`, `dice-attestation`, `secure-boot`, `device-provisioning`, `host-sdk`, `xkms-adapter`, `secure-element-se05x`, `optiga-trust-m`, `hsm-self-defence`, `application-sdk`.

## Open items outside the rows

- #1 Public-release readiness checklist: release tracking, not a SPEC row; kept for the owner and Software.
- #302 Test-suite audit (CA ruling 273): a process order, not a SPEC row; kept, moved off Dev complete.
- #346 Re-pin code-review.yml to the handbook template: CI configuration, not a SPEC row; kept for Software/Infra.
- #422 CI: OP-TEE stack restore timeout, and #424 lint-arch-leak flake fix: CI work, not a SPEC row; left to land on their own.
