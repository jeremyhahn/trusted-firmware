### Fixed

- **`test/unit/boot/test_sha256_small_mutate.c` names the mutants `src/crypto/sha256_small.c` still has, and asserts the HMAC digest** (#640): the list names the padding loop's `i != 56u` and `i == 64u` and its zero store instead of a spill test and memsets that are gone, the unobservable closing `tfw_secure_zero` calls instead of `pad` memsets, and the digest-length test checks HMAC-SHA256("abc", "abc") as well as the guard bytes.
