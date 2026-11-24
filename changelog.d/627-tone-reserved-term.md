### Added

- **`test_tone_on_a_reserved_tone_pin_is_refused_and_silent` in `test/unit/test_caps_reserved.c`** (#627): TONE at 440 Hz on the mock table's RESERVED D11, the 328P tone pin, returns `TFW_ERR_RESERVED_PIN`, no tone starts and nothing is written back, so dropping the RESERVED term from `tfw_tone_cmd_start`'s refusal mask in `src/caps/tone.c` (which would let a tone be driven onto a reserved leg) now fails the host suite.
