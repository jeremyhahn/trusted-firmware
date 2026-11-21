### Changed

- **`docs/secure-boot/architecture.md` names every tfw-boot component main has** (#640): the swap, fall-back, stage and update-request seams (the fall-back row names the rows that link `boot_prev.c`, and that the 328P and 32U4 have no PREV restore), the p256-soft verifier, and the Cortex-M, ESP32 and host HALs beside the AVR one; the data flow follows `boot_main.c`'s order.
