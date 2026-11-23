### Fixed

- **The NUCLEO-H563ZI TF-M bootloader no longer programs option bytes** (#608).
  The port's boot_hal_cfg.h defined TFM_ENABLE_SET_OB, which lets TF-M's STM32H5
  BL2 rewrite option bytes it finds set differently. The define is gone: option
  bytes are the owner's to set, and BL2 only checks them. A scripts-test check
  fails if the port defines it again or calls the HAL's option-byte programming
  functions.
