### Fixed

- **The Cortex-M README matches the MPS2 images** (#628): `docs/cortex-m/README.md` no longer says 1-Wire returns `TFW_ERR_UNSUPPORTED` (it is bit-banged on GPIO block 0 bit 0, pin 3 in the `ONEWIRE=1` row), and it names the `PWM=1`, `TONE=1`, `PULSE=1` and `ONEWIRE=1` rows and their QEMU wire rows.
