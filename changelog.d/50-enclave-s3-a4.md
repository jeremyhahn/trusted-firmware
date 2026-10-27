### Added

- **The tamper-response engine's enclave actions on the i.MX 95 M7, host
  level** (#50, `docs/hsm-self-defence/` slice 6). S3: a policy set that
  carries expected values has GET_INFO's question 17 fields compared with them
  through `tfw_hsm_port_measure`, and any mismatch raises S3. A4: each wipe key
  is deleted by the EdgeLock enclave with the strict flag and counted done only
  on its acknowledgement; otherwise the engine locks down (R3). The key-group
  delete (question 5) and A3 (question 4) are not implemented. The enclave's
  MU, key ids, the delete's id and layout, the S3 keys and the SRK revocation
  bits' place are zero placeholders in `boards/frdm-imx95-m7/board_profile.h`,
  so on the board both answer unsupported.
