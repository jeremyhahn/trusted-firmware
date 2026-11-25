### Fixed

- **docs/esp32/engineering.md cites its targets and jobs at their lines and lists every HAL file's host test** (#629). `esp32-build` is `mk/arch/esp32-targets.mk:61` and `esp32-qemu-test` `:145`; CI's `esp32-build` job is `.github/workflows/ci.yml:1410` and `esp32-qemu-test` `:1482`; `lint-conditionals`, `lint-arch-leak` and `board-sweep` are `mk/host.mk:2242`, `:2359` and `:2808`. The HAL test list adds `test_esp_selfcheck.c`, `test_esp_tone_mode_pulse.c`, `test_esp_app_ota.c`, `test_esp_wire_auth.c` and `test_esp_opsess.c`, each wired in `mk/host.mk` over a `src/hal/esp32/` file.
