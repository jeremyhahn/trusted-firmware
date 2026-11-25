### Fixed

- fix(imx95): the M7 TPM PWM takes the next prescaler when the period count is exactly 0x10000 << PS, as Linux v6.12 `pwm_imx_tpm_round_state` does, so MOD fits 16 bits and full duty's CnV no longer overflows CnV's 16-bit field into an output that is never set (`src/hal/cortex-m/imx95/tpm.h`, `test/unit/test_imx95_tpm_period.c`) (Refs #473).
