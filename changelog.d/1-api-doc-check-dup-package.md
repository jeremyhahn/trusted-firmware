### Fixed

- **`api-doc-check` no longer reads only one of two public sdk/go packages
  that share a name** (#1). It kept the last directory walked, so every
  export of the other passed with no row in docs/host-sdk/api.md; two public
  packages with one name now fail, naming both directories, before anything
  else is checked.
