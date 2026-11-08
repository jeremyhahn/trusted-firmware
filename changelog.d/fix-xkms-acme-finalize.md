### Fixed

- xkms: the e2e test CA reserves serials and signs the RFC 6962 precertificate the go-xkms v0.64.2-alpha ACME issuer asks for, so device-attest-01 finalization no longer fails with EOF and main's Integration job is green again (#335).
