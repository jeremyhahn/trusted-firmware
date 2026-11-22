### Fixed

- `test_auth_session_mutate` pins a platform-block read that delivers its bytes and then reports failure: no session opens on the operator key and the serial stays zero, so deleting either read check in `src/core/platform_id.c` fails a test (#53).
