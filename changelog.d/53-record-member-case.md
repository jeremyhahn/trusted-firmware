### Fixed

- tfwprov refuses a provisioning record with a member in another case (`"UEID"`, `"Cnf"`, `"KTY"`) or repeated within its object, and leaves the file as it is, where it read them case-insensitively, kept the last of a repeat, and `--commit` and `--verify` rewrote the file under the lowercase names (#53).
