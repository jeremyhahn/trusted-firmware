### Fixed

- tfwprov refuses a provisioning record with bytes after its JSON object (a second value, trailing text, a stray brace) and leaves the file as it is, where `--commit` and `--verify` rewrote it without them (#53).
