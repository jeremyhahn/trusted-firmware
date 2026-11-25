### Added

- **Host tests that the i.MX 95 S3 compare fails closed at its edges** (#50,
  `docs/hsm-self-defence/`). An expected value of length 0 does not match a
  length-0 field the port cannot read, a value one byte longer or shorter
  than its field is a range error even when the field's bytes agree, and a
  field ending on the GET_INFO buffer's last byte is read while one byte past
  it is a range error.
