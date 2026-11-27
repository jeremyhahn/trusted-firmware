### Added

- **The HSM payload reader's seeded fuzz tests** (#50,
  `src/core/hsm_payload.c`): the HSM policy set and script reader is read
  over 300000 seeded mutations of its valid payloads and random buffers, each
  from a buffer of exactly its length, and every input is refused or read
  exactly, inside its bounds, pinned by a host test.
