- ota: `OTA_INFO` (`0x6C`), `OTA_ACTIVATE` (`0x6D`) and `OTA_CONFIRM` (`0x6E`)
  are on the wire at the OTA ADR's decision 8 ids, in the firmware, `tfwsim`,
  the wire vectors and `sdk/go`. `OTA_ACTIVATE` is served on serial only and
  answers `TFW_ERR_UNKNOWN_CMD` over TCP.
- ota: `OTA_END` verifies and writes no MARK. `OTA_ACTIVATE` re-verifies the
  staged image and then writes it, so only an activated image is taken at reset.
- ota: `OTA_CONFIRM` no longer writes the platform block. It leaves an 11-byte
  request record (`0xC9`, 8 bytes of the measurement, CRC-16) at EEPROM `0x0EA`
  on the 2560, inside the span host `EEPROM_WRITE` refuses (now 21 bytes).
  tfw-boot confirms and settles the trial at the next reset, then clears the
  record; a stale, corrupt, torn or absent request fails the trial.
- ota: `TFW_OTA_TRIAL_TIMEOUT_S` is settled at 60 s on each AVR board with a
  boot section (open question 6).
- boot: the 2560's boot is 7 954 B (serial session) and 6 492 B (store read) of
  8 192, measured by verify; it was 7 666 and 6 256 before the confirm request.
