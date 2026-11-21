### Fixed

- **The OP-TEE TA's comments and `docs/optee-trustzone/README.md` match the code** (#640).
  The blob codec names go-xkms 06ca5ec59, the TA stores no key, `tfw-ta-check`
  lists SEAL, UNSEAL and the refused open, the derive and HKDF comments name
  what the code refuses and sizes, and the targets, GET_RANDOM row and host
  package list read as `mk/optee.mk` and the TA do.
