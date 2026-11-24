### Fixed

- **MPS2 bus-register test objects keyed by EXTRA_CFLAGS** (#628). They live in a directory named for the flag set, so a plain rebuild after `make test-asan-host` or `make coverage` no longer links instrumented objects.
