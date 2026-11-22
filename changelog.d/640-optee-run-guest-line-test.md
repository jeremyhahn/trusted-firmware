### Added

- **`scripts/test_optee_run_guest_line.py`, a test of the line `make optee-run` types at the guest's shell** (#640): one line ending in a carriage return that names no whole marker, and, run under sh with stand-in pidof and grep, the command's status from its subshell in the share and the no-tee-supplicant and no-share refusals. `make scripts-test` runs it.
