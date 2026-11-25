### Fixed

- **The runner-label test checks a block scalar label with its whitespace**
  (#615).
  `scripts/test_runner_labels.py` used to trim the decoded text of a
  `runs-on: |` or `>` block scalar, so `runs-on: |` then `ci-gate`, whose
  YAML value is `ci-gate` and a line break, passed as the allowlisted
  `ci-gate`. It now decodes the scalar as YAML does (content indentation and
  indentation indicator, folding, `-`/clip/`+` chomping) and checks the text
  untrimmed; a label with surrounding whitespace or a line break is reported
  as its repr (`w.yml:3: 'ci-gate\n'`).
