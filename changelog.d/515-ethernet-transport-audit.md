### Changed

- **The ethernet-transport requirement audit is checked again on main fa31d30e**
  (#515). Every file, test function and make variable its Requirements table
  names was found on that main, and none of the sources, board profiles or
  makefiles it names changed since the #457 audit. No requirement a host build
  can close is open. The six remaining slices are unchanged: the Mega's
  Ethernet enrolment on the bench, go-trusted-platform's device-facing
  endpoint, the owner's call on the Nano as a reference board, the shields'
  MAC values, the ATECC608C wiring, and the row's stale 328P flash figure in
  `docs/SPEC.md`.
