### Fixed

- **The ESP32 layer 0's CDI stack wipe is pinned** (#23): a host test now checks that `gate_and_hand_off` in `boot/hal/esp32/boot_l0.c` clears its stack copy of the CDI before the handoff block is written (HW Req 6.4), so removing that wipe fails the boot host suite.
