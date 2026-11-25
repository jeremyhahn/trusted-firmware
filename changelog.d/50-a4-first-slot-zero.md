### Fixed

- **The i.MX 95 A4 wipe deletes the keys it names when slot 0 is unused**
  (#50). The guard in `tfw_imx95_hsm_wipe` still read a zero first key slot as
  "no keys", so a wipe naming keys only in slots 1-3 answered
  `TFW_ERR_UNSUPPORTED` with nothing sent; the engine carried on as if A4 did
  not exist and the named keys survived. A4 is now unsupported only when no
  slot names a key. On the board nothing changes: its wipe key ids are
  placeholders, so A4 answers unsupported.
