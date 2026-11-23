### Fixed

- tfwprov refuses a provisioning record whose `ueid`, `cnf.jwk.x` or `cnf.jwk.y` is a non-canonical base64url encoding (non-zero pad bits in the last character) as not a record, and leaves the file as it is, where it read the same serial or point and then refused the record as another device's (#53).
