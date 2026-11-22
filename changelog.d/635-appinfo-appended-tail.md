### Added

- **A Go SDK test that `AppInfo` reads the known prefix of a longer
  APP_INFO reply** (#635). A 64-byte reply, a newer firmware's appended
  field after the 63-byte descriptor, is neither refused nor read into a
  32-byte unterminated Profile, as the wire protocol's appended-fields rule
  requires.
