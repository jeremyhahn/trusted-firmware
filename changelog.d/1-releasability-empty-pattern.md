### Fixed

- **A releasability pattern-list line with a class and no pattern is tested to exit 2 and name its class** (#1): `make releasability-test` now feeds the gate such a line, so the refusal cannot be dropped and leave an empty regex that matches every published line, or a rule skipped unseen; the refused-regex check also asserts the message names its class.
