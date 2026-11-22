### Added

- **`sdk/go` Attest and AttestCert page refusals each have a test** (#629): `sdk/go/attest_page_refusals_test.go` pins the reason of every refusal of a paged ATTEST or ATTEST_CERT reply (wrong page, wrong total, over-long page, short ATTEST page, total past the last page, and 15360 as the last total allowed).
