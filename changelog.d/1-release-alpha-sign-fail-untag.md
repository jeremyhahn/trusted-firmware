### Fixed

- **`make release-alpha` is tested to leave no tag when signing the second tag fails** (#1): a signature that fails after the first tag is cut leaves no tag in the checkout and pushes nothing to `dev`, so the next run cuts the same version.
