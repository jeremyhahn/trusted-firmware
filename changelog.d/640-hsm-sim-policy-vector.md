### Fixed

- **The simulator's policy envelope is held to `testdata/suit_vectors.h`**
  (#640). `sdk/go/hsm_record_tfwsim_test.go` keeps a copy of
  `SUIT_POLICY_ENVELOPE`; a new test reads the header and requires the copy to
  equal it byte for byte, so a re-signed or reshaped vector fails the Go test.
