### Fixed

- **`make lint-no-delay`** (#627) joins a backslash-newline outside a literal or
  comment before matching, so a delay call spliced before its parenthesis or
  inside its name (`_delay_ms\` then `(10);`) is a finding, named on the first
  physical line of the join.
