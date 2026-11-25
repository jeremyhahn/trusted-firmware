### Added

- **A host test that one i.MX 95 S1 or S5 event stays one signal** (#50,
  `docs/hsm-self-defence/`). One tamper notification and one hotplug pin
  interrupt raise one signal each, and three later passes with no new event
  raise nothing more, so an intake that never takes the counter in cannot
  re-raise the same event every other pass.
