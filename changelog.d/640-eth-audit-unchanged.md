### Fixed

- **The ethernet-transport audit no longer claims no named makefile changed
  since d1282f61** (#640): `mk/host.mk` did, and the audit, plan 515 and its
  changelog entry now say its cited target `lint-conditionals` was reviewed and
  is unchanged.
