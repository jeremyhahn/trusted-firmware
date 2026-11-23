### Changed

- **The HSM channel's own refusal keeps the held set** (#50,
  `docs/hsm-self-defence/`): the counted refusal of an empty or oversized
  `HSM_LOAD` envelope keeps the held set, and a whole envelope after a dropped
  chunk is held, each pinned by a host test.
