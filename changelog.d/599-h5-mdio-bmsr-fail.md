### Added

- **Host tests pin the STM32H5 PHY scan's failed-BMSR branch and the boot line's
  exact-fit boundary** (#599). A PHY whose IDs read but whose first BMSR read
  fails is still reported, with link down and no second BMSR read; the boot line
  fits a buffer of exactly its length plus one and leaves a buffer one byte
  shorter untouched, for both the PHY line and "eth: phy none".
