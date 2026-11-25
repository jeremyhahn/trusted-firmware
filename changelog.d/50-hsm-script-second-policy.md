### Added

- **A host test that a script named only by a policy past the first is held**
  (#50, `docs/hsm-self-defence/`). A set whose second policy alone names a
  script accepts that script, the next set that names it keeps it, and once
  that policy is selected its rule runs the script instead of locking down,
  so the engine cannot look for names in the first policy only.
