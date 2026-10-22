### Added

- **`CRYPTO=optiga` links the OPTIGA Trust M provider** (row
  `optiga-trust-m`). The tier's object list is the I2C transport, the
  read-only command layer, the provider and the shared software SHA-256. It
  carries the operator-signature auth session, under the same profile knob as
  the element tier, and no alias certificate, because the provider cannot read
  its public key. OTA_INFO's rollback floor is the platform block's, as on
  the soft tier. `PROVISION=1` on this tier stops the build: provisioning
  writes the element, which the onboarding ADR forbids until the owner rules.

  No board's `BOARD_TIERS` names the tier; which boards it fits is the ADR's
  open question 5. The ATmega2560 builds it as two measurement rows,
  `optiga:ETH=1` and `optiga:ETH=0`, so `make firmware-matrix` size-checks it
  and its flash cost is a number. No default image changes. No wire vector,
  command id or status code changes.

### Changed

- `make platform-bake-no-link-check` checks each `BOARD_ROWS` row with its
  own settings. It used to skip them, which was safe only while every row
  varied ETH alone.
