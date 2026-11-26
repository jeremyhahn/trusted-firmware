### Fixed

- The OP-TEE build's default job count now follows the memory the build may use as well as the CPUs, so a cold-cache build in a memory-limited container is not killed.
