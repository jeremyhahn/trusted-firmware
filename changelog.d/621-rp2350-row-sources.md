### Added

- The Pico 2 W's row test pins what the I2C=0 and PERIPH=0 rows leave out: each row's `BOARD_DEFS` carries only its own `-DTFW_CM_I2C=1`/`-DTFW_CM_PERIPH=1` switch, the I2C=0 rows link `src/caps/bus_none.c` and no bus driver, and the PERIPH=0 rows link no PWM or tone sources (`scripts/test_rp2350_rows_mk.py`, Refs #621).
