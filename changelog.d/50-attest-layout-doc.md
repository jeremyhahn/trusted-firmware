### Fixed

- **`hsm_act.h` says what the i.MX 95 A4 wipe and S3 attestation send for a
  layout the client refuses** (#50). Both comments said `TFW_ERR_UNSUPPORTED`
  "with nothing sent", but the layout is refused only after the session (and,
  for the wipe, the key store and key management service) is opened; those are
  closed again and no delete or attestation is sent. A new host test pins the
  S3 case. No behaviour change; on the board A4 and S3 stay unsupported.
