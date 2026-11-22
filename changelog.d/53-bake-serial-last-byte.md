### Fixed

- The bench bake image's zero-serial refusal is now pinned on both sides: a serial nonzero only in its last byte, or with a zero first byte, is an identity and is baked, so a check that reads only the first serial byte or stops one byte short fails the host tests (#53).
