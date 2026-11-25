### Added

- test/unit/test_esp_selfcheck.c tests that the P4 self-check stops a check at the step that failed: a refused tone start, a refused PWM write or a failed re-read of the restored sector (Refs #48).
