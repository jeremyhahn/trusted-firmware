- `make coverage` now counts the crypto tiers: `src/crypto/sha256.c`, the
  soft, none and ATECC608A providers and driver, `boot/measure/` and
  `boot/verify/`. New tests pin the ATECC608A status-byte table, the soft and
  none tiers' refusal to verify a signature, and the donna shim's zero fill.
  `boot/tier/cdi_*.c` and `src/crypto/soft/uds_flash.c` stay out: they are
  built only for AVR (#75, #77).
- The element model gains two fault hooks (a corrupted read, a NACKed
  command), and new tests drive the ATECC608A driver's, provider's and boot
  measure's refusal and bus-error paths. Branch coverage, before and after:
  `src/crypto/atecc608a/atecc.c` 72% → 97%, `src/crypto/atecc608a/provider.c`
  70% → 97%, `boot/measure/measure_atecc608a.c` 66% → 87% (#75, #77).
