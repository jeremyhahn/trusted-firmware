### Added

- **A host test pins the STM32H5 clock raise and the watchdog** (#599).
  `test_h5_hal_soc` runs `src/hal/cortex-m/stm32h5/hal_soc.c` over a register
  model under the stm32h573i-dk profile: the early bring-up pets the IWDG
  rather than touching its configuration, writes the flash wait states and waits
  for them to read back before it clears the HSI divider, then waits on HSIDIVF,
  keeping every other bit; the watchdog arms for 4 s of the LSI in key order and
  waits on its status before the first reload; the pet is a reload only. The
  early bring-up and the pet run over an inherited, armed PR and RLR, and the
  model counts every access, so even a store of zero or of the value already
  there fails the test.
