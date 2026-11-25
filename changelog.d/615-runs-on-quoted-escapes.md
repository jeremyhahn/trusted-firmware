### Fixed

- **Quoted runner labels are decoded as YAML decodes them** (#615).
  `scripts/test_runner_labels.py` removed only the quotes around a label, so
  an escaped label such as `"\x63odex"` (`codex`) failed `make scripts-test`,
  `'qw''ne'` was compared as `qw''ne` and a multi-line quoted label kept its
  escaped line break. It now decodes double-quoted escapes, the doubled
  single quote and the line folding of a multi-line quoted scalar before
  comparing the label; an unknown escape is left undecoded, so the label
  still fails closed.
