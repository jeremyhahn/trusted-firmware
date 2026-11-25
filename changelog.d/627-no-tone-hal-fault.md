### Added

- **`test_a_stop_the_hal_refuses_passes_back_with_no_reply` in `test/unit/test_caps_mutate.c` and the mock hook `tfw_mock_tone_stop_fails` in `test/mock/hal_mock.c`** (#627): with the mock's `tfw_hal_tone_stop` refusing (`TFW_ERR_INTERNAL`, as the ESP32's `ledc_stop` or an i.MX95 with no GPT can), NO_TONE now passes the refused stop back with no reply bytes and the tone still sounding, so a `tfw_tone_cmd_stop` in `src/caps/tone.c` that dropped the HAL's status and answered `TFW_OK` now fails the host suite.
