### Added

- **HSM wiped-state and ELE get-event CRC host tests** (#50,
  `src/core/hsm_cmd.c`, `src/hal/cortex-m/imx95/ele_mu.c`): HSM_STATE reports
  the wiped flag only after an acknowledged, corroborated wipe, never after a
  refused or uncorroborated one, and an i.MX 95 ELE get-event request longer
  than four words ends in its CRC word while a four-word one keeps its last
  word, pinned by host tests.
