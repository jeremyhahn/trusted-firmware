### Fixed

- **The Cortex-M README matches the MPS2 images** (#628): `docs/cortex-m/README.md` no longer says 1-Wire returns `TFW_ERR_UNSUPPORTED` (it is bit-banged on GPIO block 0 bit 0, pin 3 in the `ONEWIRE=1` row), and it names the `PWM=1`, `TONE=1`, `PULSE=1` and `ONEWIRE=1` rows and their QEMU wire rows.
- **`docs/cortex-m/architecture.md` names the one linker script** (#628): `mk/arch/cortex-m/image.ld`, with a SoC's own script only where its `profile.mk` names one; `mps2.ld` and `<SOC>.ld` do not exist.
- **The jump is in `boot/hal/cortex-m/boot_jump.c`** (#628), not `boot_hal_cortex-m.c`.
- **The feature examples match the MPS2 boards** (#628): a capability turned off is `TFW_FEATURE_ANALOG 0` (EEPROM is on), and the named-row features are I2C, SPI, TONE, PULSE, ONEWIRE and PWM, each in its own build directory.
- **The trust boundaries name the MPU and the SAU** (#628): mps2-an500 arms the MPU, the `TZ=1` row programs the SAU and enters a Non-secure image, and the default mps2-an505 image programs neither.
- **"What the row does not do yet" no longer denies shipped work** (#628): the vendor SoC ports, the Non-secure world, the MPU and the PWM, tone, pulse and 1-Wire rows; TF-M is stated as the ADR decides it.
- **The Cortex-M README's SPEC row is "in progress"** (#628), as in `docs/SPEC.md`.
