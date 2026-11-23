### Fixed

- **ADR #15's remaining Secure Manager, vendor TF-M and PSA token offers are
  marked not taken** (#586). The passages in
  `docs/cortex-m/adr-trust-model-vs-tf-m.md` that offer ST's Secure Manager, a
  vendor's TF-M build or the PSA token are kept as the analysis first recorded
  and now carry a **not taken** marker per ruling 935, and its secure-world
  table names upstream TF-M Profile Medium per ruling 1024. Documentation only.
