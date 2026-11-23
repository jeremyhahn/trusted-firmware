### Added

- **`scripts/test_tool_pins.py`, a test of the toolchain image's pin file** (#629). `scripts/tool-pins.sh` has to be plain NAME=value lines that a shell and `scripts/deps-check.py` read the same way, its digests have to be well formed, and GO_IMAGE and ESP_IDF_VERSION have to match GO_VERSION and `mk/arch/esp32.mk`. `make scripts-test` runs it.
