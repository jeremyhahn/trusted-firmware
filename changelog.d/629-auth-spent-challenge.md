### Changed

- **A spent challenge refuses a second answer even over the zeroed nonce**
  (#629). `test_a_spent_challenge_refuses_a_signature_over_the_zeroed_nonce`
  in `test/unit/test_auth_robust.c` sends a rejected AUTH_RESPONSE, then one
  the operator signed over the all-zero nonce the spent challenge leaves
  behind, and checks it is refused and the gate stays shut. It holds
  `src/core/auth_session.c` line 89, `challenge_pending = 0u;` in
  `tfw_auth_cmd_response`, which no test held before.
