### Added

- **The i.MX 95's Cortex-M7 as a board of the Cortex-M port** (SPEC row
  `imx95`). `boards/frdm-imx95-m7/` is the FRDM-IMX95-PRO's M7 as data, board
  id `0x41` (provisional, design question 20), with the TCM map at the M7's
  addresses (`mk/arch/cortex-m/imx95.ld`) and the SoC directory
  `src/hal/cortex-m/imx95/`: LPUART3, the RGPIO banks and WDOG5, with no pin
  mux, clock or power, which NXP's System Manager owns. The divisor search and
  the bank table are tested on the host (`test/unit/test_imx95_soc_calc.c`).
  The application carries an empty remoteproc resource table in
  `.resource_table` (`test/unit/test_imx95_rsc_table.c`), so Linux's
  remoteproc can load it; whether that loader is supported is design question
  19. `firmware-matrix` builds the board on the soft root; nothing runs it,
  as QEMU has no i.MX 95, and every SoC fact is to be confirmed against
  IMX95RM.
- **The board in the board sweeps.** `board-check` knows the part
  (`mimx9596-m7`: 256 KiB ITCM, 256 KiB DTCM, tfw-boot's reserved 32 KiB at
  the ITCM's base), the board-id registry has the `0x41` row, and
  `sdk/go` names it `BoardFRDMIMX95M7` (`frdm-imx95-m7`).
- **The EdgeLock Enclave's message layer on the M7.**
  `src/hal/cortex-m/imx95/ele_mu.c` sends one request through the enclave's
  MU and reads one response, polling at most 100000 reads per word. It
  checks the response's version, tag, command, size and status, refusing
  each with its own result. It implements GET_INFO (0xDA), which needs no
  key or session.
  - Its values are imx-secure-enclave's and U-Boot's, which agree. RM00284
    Rev. 4.5 defines no wire format.
  - The MU registers are Linux's, unconfirmed against IMX95RM.
  - Nothing calls it until design question 3 is answered.
  - There is no key, attestation or crypto tier yet.
- **The enclave session, open and close.** `ele_mu.c` opens a session
  (SESSION_OPEN, 0x10: three words out, three back with the session handle)
  and closes it (SESSION_CLOSE, 0x11: the handle out, the status back). The
  caller keeps the handle; the HAL holds none. A close the enclave refuses
  has its own result, `TFW_ELE_E_HANDLE`.
  - The ids and structures are imx-secure-enclave d7e1180b's
    (`sab_msg_def.h`, `sab_session.h`), in the layout its ELE build takes.
    RM00284 §3.1 is the library call over them.
  - Nothing calls it until design question 3 is answered.
- **The enclave key store, open and close.** `ele_mu.c` opens a key store
  in a session (KEY_STORE_OPEN, 0x30: six words out, the session handle, the
  store's id, a nonce, the flags and a CRC word; three back with the key
  store handle) and closes it (KEY_STORE_CLOSE, 0x31: the handle out, the
  status back). It creates or loads, and sends no other flag: no STRICT,
  since nothing on the M7 serves the enclave's NVM writes. The caller keeps
  both handles. An open with no session, or on one the enclave has closed,
  has its own result, `TFW_ELE_E_SESSION`.
  - The ids, structures, flags and CRC are imx-secure-enclave d7e1180b's
    (`sab_msg_def.h`, `sab_key_store.h`, `hsm_key_store.h`, `ele_utils.c`).
    RM00284 §3.22 is the library call over them.
  - Nothing calls it until design questions 3 and 4 are answered.
- **The enclave key management service, open and close.** `ele_mu.c` opens
  the key management service on a key store (KEY_MANAGEMENT_OPEN, 0x40: six
  words out, the key store handle, two address extensions and the flags, all
  zero as the library sends them, and a CRC word; three back with the key
  management handle) and closes it (KEY_MANAGEMENT_CLOSE, 0x41: the handle
  out, the status back). The caller keeps every handle. An open with no key
  store, or on one the enclave has closed, has its own result,
  `TFW_ELE_E_KEY_STORE` (11).
  - The ids and structures are imx-secure-enclave d7e1180b's
    (`sab_msg_def.h`, `sab_key_management.h`). RM00284 §3.2.6.5 and
    §3.2.6.6 are the library call over them.
  - No key is generated or used yet; nothing calls it until design
    questions 3 and 4 are answered.
- **One NIST P-256 key generated in the enclave.** `ele_mu.c` sends
  KEY_GENERATE (0x42: twelve words out, the key management handle, the key
  id, group, type 0x7112 at 256 bits, lifetime, usage, permitted algorithm
  and the public key's address, and a CRC word; four back with the key id).
  The enclave writes the 64-byte public key into the caller's buffer.
  - Only a volatile key is asked for. A persistent one needs the STRICT
    operation and the NVM storage messages this firmware does not serve.
  - Three results are appended:
    - `TFW_ELE_E_KEY_MGMT` (12): no key management service, or one the
      enclave has closed;
    - `TFW_ELE_E_KEY_TYPE` (13): another key type or lifetime;
    - `TFW_ELE_E_BUFFER` (14): a buffer under 64 bytes.
  - The ids and structures are imx-secure-enclave d7e1180b's
    (`sab_msg_def.h`, `sab_key_generate.h`, `hsm_key.h`).
    `hsm_generate_key`, in RM00284 §3.2's key management group, is the
    library call over them.
  - The key is not used yet: no signature, attestation or deletion. Nothing
    calls it until design questions 3 and 4 are answered.
- **One ECDSA P-256 signature from the enclave.** `ele_mu.c` opens and
  closes the signature generation service on the key store (0x70: six words
  out, three back with the service's handle; 0x71: two and two), and sends
  SIGNATURE_GENERATE (0x72: fourteen words out, the service's handle, the key
  id, the digest's and the signature buffer's addresses, their sizes, scheme
  0x06000609 ECDSA with SHA-256, and a CRC word; three back). The input is
  the 32-byte SHA-256 digest, not the message. The enclave writes r||s, 64
  bytes, into the caller's buffer.
  - A key other than NIST P-256 or another scheme is `TFW_ELE_E_KEY_TYPE`, a
    buffer under 64 bytes `TFW_ELE_E_BUFFER`, both unsent.
  - One result is appended: `TFW_ELE_E_SIGN_GEN` (15), no signature
    generation service, or one the enclave has closed.
  - The values are imx-secure-enclave d7e1180b's (`sab_msg_def.h`,
    `sab_sign_gen.h`, `hsm_sign_gen.h`). The signature generation calls in
    RM00284 §3.2 are the library calls over them.
  - Nothing calls it until design questions 3 and 4 are answered.
- **The public key exported and a signature verified by the enclave.**
  `ele_mu.c` sends PUB_KEY_RECOVERY (0x32: seven words out, the key store
  handle, the key id, the caller's buffer's address and size, and a CRC word;
  three back), and the enclave writes X||Y, 64 bytes, into the caller's
  buffer. It opens and closes the signature verification service on the
  session (0x80: six words out, three back with the service's handle; 0x81:
  two and two), and sends SIGNATURE_VERIFY (0x82: fourteen words out, the
  service's handle, the plaintext key's, the digest's and the signature's
  addresses, their sizes, key type 0x4112 at 256 bits, scheme 0x06000609, and
  a CRC word; three back with the verdict).
  - A key other than NIST P-256 or another scheme is `TFW_ELE_E_KEY_TYPE`; a
    buffer, key or signature under 64 bytes `TFW_ELE_E_BUFFER`; both unsent.
  - Two results are appended: `TFW_ELE_E_SIGN_VER` (16), no signature
    verification service or one the enclave has closed; `TFW_ELE_E_SIG_BAD`
    (17), a signature the enclave checked and found not to hold, told apart
    from a transport error.
  - The values are imx-secure-enclave d7e1180b's (`sab_msg_def.h`,
    `sab_key_recovery.h`, `sab_verify_sign.h`, `hsm_verify_sign.h`,
    `hsm_key.h`). `hsm_pub_key_recovery` and the signature verification calls
    in RM00284 §3.2 are the library calls over them.
  - Nothing calls it until design questions 3 and 4 are answered.
