### Fixed

- tfwprov refuses a provisioning template with no `name` line or an empty `name =`, the key the engineering contract lists as required, where it loaded it and `--dry-run` printed `plan (template "")` (#53).
