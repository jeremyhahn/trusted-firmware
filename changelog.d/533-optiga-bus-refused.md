### Changed

- **The OPTIGA Trust M ADR records the bus-off refusal as tested** (#533).
  `docs/multi-arch/adr-optiga-trust-m-onboarding.md`: R15 names
  `scripts/test_optiga_bus_refused.py`'s two tests, and slice 2 is done; with
  mk/firmware.mk's `$(error)` commented out, the refusal test fails.
