### Fixed

- **The OTA audit cites the right evidence for `OTA_CONFIRM`** (#640): `docs/ota/engineering.md` requirement 25 now names `boot/boot_swap.c`'s `confirm_requested()` and its signed, wrong-signature and other-measurement cases in `test/unit/boot/test_boot_swap.c`, in place of the unrelated request-handoff test; remaining slice 2 is proved by `make integration-test-boot-prev BOARD=atmega32u4` and the supported-row size check, since `integration-test-boot` never runs the 32U4 restore. Docs only.
