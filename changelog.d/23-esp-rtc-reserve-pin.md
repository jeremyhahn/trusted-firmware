### Added

- **The ESP32 RTC handoff reserve is pinned to `TFW_BOOT_HANDOFF_LEN` on the
  host** (#23, row `esp32`). `scripts/test_esp32_rtc_reserve.py` checks that
  the arch sdkconfig's `CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE`, every
  `-D` of it in the host rules and the boot HAL test's `custom[]` stand-in
  equal `TFW_BOOT_HANDOFF_LEN`, that the reserve is on and outside the CRC,
  and that no board sdkconfig overrides it; planted mutations of each file
  must be caught.
