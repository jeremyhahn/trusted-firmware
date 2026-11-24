### Fixed

- **`make release-alpha` deletes the tags it cut when the cut or the push to `dev` fails** (#1): a refused push no longer leaves two signed tags for an unreleased version in the checkout, so the next run cuts the same version instead of skipping one.
