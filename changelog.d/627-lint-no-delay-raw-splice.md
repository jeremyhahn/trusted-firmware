### Fixed

- **`make lint-no-delay`** (#627) deletes every backslash-newline before it
  recognises comments and literals, so a comment closed by a spliced `*\` and
  `/` no longer hides a delay call after it, and a spliced `/` and `/` or `/`
  and `*` opens a comment.
