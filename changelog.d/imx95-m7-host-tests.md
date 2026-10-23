### Added

- **The i.MX 95 M7 board's HAL tested on the host** (SPEC row `imx95`).
  QEMU has no i.MX 95, so `test/unit/test_imx95_hal.c` runs the SoC
  directory's UART, pin and watchdog code unchanged against a register model
  of LPUART3, the RGPIO banks, WDOG5 and the NVIC (`test/model/imx95_model.c`),
  which the host build substitutes for the core's register header
  (`test/model/cortex-m/cm_regs.h`). `test/unit/test_imx95_rsc_table.c` now
  reads the image's remoteproc resource table as Linux's loader does and
  refuses each corrupted field for its own reason. The register layouts are
  still to be confirmed against IMX95RM.
- **The enclave's MU in the register model.** `test/model/imx95_model.c` adds
  the ELE MU's PAR, TSR, RSR and transmit and receive registers, and counts
  any word read or written out of turn. `test/unit/test_imx95_ele_mu.c` runs
  the message layer against it with an enclave written in the test, every
  word a literal. Its cases:
  - a good GET_INFO and its information buffer;
  - words wrapping round the registers;
  - a wrong version, tag, command or size, and a failure status, each
    refused with its own result;
  - a response that never comes, or stops after the header, times out;
  - a transmit register that never empties times out;
  - an MU with no registers, and an over-long request, are refused unsent;
  - a session opened and its handle closed; an open or a close refused for
    a failure status, a wrong size or another command echoed, each with its
    own result; either one never answered times out;
  - a key store created or loaded, its CRC word, and its handle closed; the
    session's handle carried into the open; an open refused for a failure
    status, a closed session, no session, a wrong size or another command
    echoed, and a close refused likewise, each with its own result; either
    one never answered times out;
  - the key management service opened on a key store, its CRC word, and its
    handle closed; an open refused for a failure status, a closed store, no
    store, a wrong size or another command echoed, and a close refused
    likewise, each with its own result; either one never answered times out;
    the handles carried from session to key store to key management and
    closed back in order;
  - a NIST P-256 key generated, every request word and its CRC word, the key
    id and the 64-byte public key returned; a generate refused for a failure
    status, a closed service (with and without the status word), a wrong size
    or another command echoed, each with its own result; no service, a buffer
    under 64 bytes, another key type and a persistent lifetime refused
    unsent; one never answered times out; the handles carried from session to
    a generated key and closed back in order;
  - the signature generation service opened and closed, every request word
    and its CRC word, refused for a failure status, a closed store, no store,
    a wrong size or another command echoed, and a close of a closed handle
    refused;
  - an ECDSA P-256 signature over a SHA-256 digest, every request word and
    its CRC word, the 64-byte r||s in the caller's buffer; a sign refused for
    a failure status, a closed service, a wrong size or another command
    echoed; no service, another key type, a key other than P-256, a buffer
    under 64 bytes and another scheme refused unsent; one never answered
    times out; the handles carried from session to a signature and closed
    back in order;
  - a public key export, every request word and its CRC word, the 64-byte
    X||Y in the caller's buffer; refused for a failure status, a closed
    store, a wrong size or another command echoed; no store, another key
    type, a key other than P-256 and a buffer under 64 bytes refused unsent;
    one never answered times out;
  - the signature verification service opened on the session and closed,
    every request word and its CRC word; refused for a closed session, no
    session, and a close of a closed handle;
  - an ECDSA P-256 verify over a SHA-256 digest, every request word and its
    CRC word; a verdict that passes, and one that fails as its own result; a
    verify refused for a failure status, a closed service, a wrong size or
    another command echoed; no service, another scheme, a key other than
    P-256, a short key and a short signature refused unsent; one never
    answered times out; a key generated, used to sign, exported, verified
    and every handle closed back in order.
