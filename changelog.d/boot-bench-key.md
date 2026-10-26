### Added

- **A development-only bench signing key, kept outside the tree** (#7).
  `BENCH_KEY` names it, under `$HOME` by default; a path inside the repository
  tree, through a link or not, is refused before anything runs. Only
  `make bench-key` creates it, P-256 and mode 600. `make boot-bench` writes
  `tfw-boot-bench.hex`, the soft tier's tfw-boot with the key's public half in
  `tfw_boot_p256_key` and nothing else changed; the unpatched image is left as
  it was. `make bench-sign` signs `image-tbs`'s span with the key and writes the
  64-byte r||s `image-seal` takes. `scripts/test_bench_key.py`, in
  `make scripts-test`, runs each target with throwaway keys and checks that an
  image signed this way verifies against the public half the bench image
  carries. No wire vector changed.
