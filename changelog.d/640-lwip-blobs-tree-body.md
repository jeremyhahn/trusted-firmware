### Fixed

- **`scripts/lwip_upstream_check.py` no longer desyncs after a path that is a directory upstream** (#640): `upstream_blobs` steps past a tree's body in the `git cat-file --batch` reply, so every later path still gets its own blob or None.
