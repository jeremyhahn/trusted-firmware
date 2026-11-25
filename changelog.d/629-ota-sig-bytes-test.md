### Added

- **`sdk/go/cmd/tfwctl/otaauth_test.go`, a test that `takeOTASig` keeps the row key's signature bytes and the other arguments in order** (#629): 64 distinct bytes through `--sig=`, a raw `--sig-file=` and an upper-case hex file come back byte for byte with the rest in order, and each refusal (a second flag, a hex or raw file of the wrong length, a file that cannot be read) and `needOTASig`'s text are pinned.
