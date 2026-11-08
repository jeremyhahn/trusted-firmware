### Fixed

- **`make releasability-test` runs the gate on its own fixture rules.**
  Its lab-address and lab-host plants are neutral stand-ins, and five of
  them matched no rule in the fleet pattern list the gate read, so the gate
  passed them (exit 0, want 1). The test now writes one fixture rule per
  plant and hands that list to the gate.
- **`xkms` carries no module behind without a recorded reason.** Its
  `go.mod` and `go.sum` take the module moves of #339, #340 and #341: each
  module is at its latest or held with a reason, so `make xkms-deps-check`
  passes (#324).
