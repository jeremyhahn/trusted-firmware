### Fixed

- **A test now pins that the DeviceID signer's yield hook runs only inside a
  sign** (#53). The RFC 6979 signer and the DeviceID signer call
  `tfw_sign_yield` during their ladder (the DeviceID sign during its scalar
  derivation too), and a public-key derivation after either sign calls it
  none. Before, deleting the hook's set in `tfw_p256_sign_rfc6979`, or its
  clear in `src/crypto/deviceid_p256.c`, passed every test; with the clear
  gone the next DeviceID public-key derivation (PROV_GENKEY, PROV_PUBKEY)
  would have run the H5 ETH image's deadline sweep outside any sign.
