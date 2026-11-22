### Added

- **A unit test that the DICE partition's start tier reaches the alias
  certificate** (#608). Starting the partition with the secure-element tier
  keeps the alias key and changes the certificate, so a partition that drops
  the tier fails `test_dice_partition`.
