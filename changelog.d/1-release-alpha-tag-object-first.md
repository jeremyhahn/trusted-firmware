### Fixed

- **`make release-alpha` no longer deletes a tag replaced before its ownership check** (#1): a tag replaced between this run's cut and its ownership check is no longer deleted by the failure cleanup; the run records the id of the tag object it made and publishes the ref create-only, never rereading it.
