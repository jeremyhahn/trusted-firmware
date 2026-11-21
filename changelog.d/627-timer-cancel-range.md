### Fixed

- **TIMER_CANCEL refuses an `apply_final` above one with TFW_ERR_RANGE**
  (#627), as docs/device-capabilities/api.md gives the byte as (0|1) and as
  TIMER_SET and DIGITAL_WRITE already refuse their level byte. A byte of
  2..255 was taken as "apply" and drove the pin; now the timer stays armed.
