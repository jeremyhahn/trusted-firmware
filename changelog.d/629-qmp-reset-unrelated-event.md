### Added

- **Tests pin both of `qmpReset`'s RESET guards** (#629): an unrelated event after `system_reset`'s return does not end the wait for the reset, and a RESET sent while `qmp_capabilities` is pending is not counted (`sdk/go/internal/peer/qmp_test.go`).
