### Fixed

- **The i.MX 95 A4 wipe deletes every key it names** (#50). The delete loop
  stopped at the first zero key slot, so a key named after an unused slot was
  never deleted while `tfw_imx95_hsm_wipe` still answered `TFW_OK` and the
  engine counted the wipe done. Zero slots are now skipped and all four slots
  are tried. On the board nothing changes: its wipe key ids are placeholders,
  so A4 answers unsupported.
