### Changed

- **A request one byte past the maximum payload is pinned end to end** (#640). The framer refuses it as a format error and resyncs, the bootloader sends no answer to it and does not count it as session progress, and the next request is still answered. Tests only.
