### Changed

- **`scripts/verify-vectors.sh` has a test** (#629): `scripts/test_verify_vectors.py` pins the real C/Go pair's counts and makes each kind of drift (CRC, frame, SYS_INFO, CAPS swap or absence, trailer span) a failure.
