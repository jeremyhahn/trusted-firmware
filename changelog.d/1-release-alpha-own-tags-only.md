### Fixed

- **`make release-alpha` deletes only its own tags after a failure** (#1): a failed cut or push deletes only the tags this run created, and only while they still point at this run's tag objects, so a second run that picked the same version can no longer delete the first run's tags.
