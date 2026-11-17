### Changed

- **docs/device-onboarding: stale facts refreshed** (#523). The Ethernet
  allow-list now lists the OTA staging ids `0x68`–`0x6B`; DHCP is documented as
  supported; R5 and open question 4 name the go-xkms v0.64.2-alpha and `sdk/go`
  v0.4.2-alpha that `xkms/go.mod` pins; the e2e tests are cited in
  `xkms/e2e/soft_enroll_test.go` and `hw_enroll_test.go`, and `ATTEST_CSR`'s Go
  check in `sdk/go/cmd/tfwctl/e2e_attestcsr_test.go`.
