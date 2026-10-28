/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The EdgeLock Enclave's message layer as the i.MX 95 M7 would speak it
 * (SPEC row `imx95`): one request out through a Messaging Unit's transmit
 * registers, one response back through its receive registers, polled with a
 * bounded wait. The commands here are GET_INFO, the open and close of the
 * session, the key store and the key management service, and KEY_GENERATE
 * for one NIST P-256 key; no key is used.
 *
 * NXP RM00284 Rev. 4.5, "EdgeLock Enclave Hardware Security Module API", is
 * the library's API and holds none of this: it names no message header,
 * status word, command id or MU register. Its section 1 names the reference
 * implementation, nxp-imx/imx-secure-enclave, and every value below is that
 * implementation's at commit d7e1180b (src/common/sab_msg_def.h,
 * src/common/sab_dev_getinfo.h), with NXP's U-Boot at ece349ad agreeing word
 * for word (arch/arm/include/asm/mach-imx/ele_api.h,
 * drivers/misc/imx_ele/ele_mu.c). The tags are U-Boot's alone: the library
 * leaves them to the kernel driver. docs/reference/README.md lists all three.
 *
 * The MU registers are the ELE MU's in Linux v6.12
 * drivers/mailbox/imx-mailbox.c ("fsl,imx95-mu-ele"), a secondary source;
 * they are unconfirmed against IMX95RM, which this project does not hold.
 * Which MU, if any, joins the M7 to the enclave is question 3 of
 * docs/multi-arch/imx95-design.md, so the channel is the caller's data.
 */
#ifndef TFW_CM_IMX95_ELE_MU_H
#define TFW_CM_IMX95_ELE_MU_H

#include <stdint.h>

/* The header word, little-endian bytes: version, size in words counting the
 * header, command, tag (sab_msg_def.h struct sab_mu_hdr; ele_api.h struct
 * ele_msg). */
#define TFW_ELE_HDR_VERSION(h) ((h) & 0xFFu)
#define TFW_ELE_HDR_SIZE(h)    (((h) >> 8) & 0xFFu)
#define TFW_ELE_HDR_COMMAND(h) (((h) >> 16) & 0xFFu)
#define TFW_ELE_HDR_TAG(h)     (((h) >> 24) & 0xFFu)

#define TFW_ELE_VERSION  0x06u   /* MESSAGING_VERSION_6, ELE_VERSION */
#define TFW_ELE_TAG_CMD  0x17u   /* ELE_CMD_TAG */
#define TFW_ELE_TAG_RSP  0xE1u   /* ELE_RESP_TAG */

/* GET_INFO (ROM_DEV_GETINFO_REQ, ELE_GET_INFO_REQ): the request carries the
 * buffer's address, high word first, and its length; the response is the
 * header and the status word, and the enclave writes the information into
 * the buffer (sab_dev_getinfo.h; ele_api.c ele_get_info). */
#define TFW_ELE_GET_INFO           0xDAu
#define TFW_ELE_GET_INFO_CMD_WORDS 4u
#define TFW_ELE_GET_INFO_RSP_WORDS 2u
#define TFW_ELE_GET_INFO_LEN       256u   /* dev_info + dev_addn_info + ver3 */

/* SESSION_OPEN and SESSION_CLOSE (SAB_SESSION_OPEN_REQ, SAB_SESSION_CLOSE_REQ;
 * sab_msg_def.h, src/common/include, lines 26 and 27). The structures are
 * sab_session.h's (src/common/sab_msg/include, lines 11 to 44) in the layout
 * the ELE build takes: src/plat/ele/ele.mk line 39 defines PSA_COMPLIANT, so
 * the open request's first word is a reserved byte, the MU's interrupt index
 * and a reserved half-word, and its second the priority and operating mode,
 * which sab_session.c never sets and so leaves zero. The open response is
 * the header, the status word and the session handle; the close request is
 * the header and that handle, its response the header and the status word.
 * RM00284 section 3.1 (hsm_open_session, hsm_close_session) is the library
 * call over these messages. */
#define TFW_ELE_SESSION_OPEN            0x10u
#define TFW_ELE_SESSION_CLOSE           0x11u
#define TFW_ELE_SESSION_OPEN_CMD_WORDS  3u
#define TFW_ELE_SESSION_OPEN_RSP_WORDS  3u
#define TFW_ELE_SESSION_CLOSE_CMD_WORDS 2u
#define TFW_ELE_SESSION_CLOSE_RSP_WORDS 2u

/* KEY_STORE_OPEN and KEY_STORE_CLOSE (SAB_KEY_STORE_OPEN_REQ,
 * SAB_KEY_STORE_CLOSE_REQ; sab_msg_def.h lines 42 and 43). The structures
 * are sab_key_store.h's (src/common/sab_msg/include, lines 11 to 50) in the
 * PSA_COMPLIANT layout: the open request is the header, the session handle,
 * the key store id, the nonce (the field the library calls `password`), a
 * word whose third byte is the flags and whose others are reserved, and a
 * CRC word; the open response is the header, the status word and the key
 * store handle; the close request is the header and that handle, its
 * response the header and the status word. RM00284 section 3.22
 * (hsm_open_key_store_service, hsm_close_key_store_service) is the library
 * call over these messages. */
#define TFW_ELE_KEY_STORE_OPEN            0x30u
#define TFW_ELE_KEY_STORE_CLOSE           0x31u
#define TFW_ELE_KEY_STORE_OPEN_CMD_WORDS  6u
#define TFW_ELE_KEY_STORE_OPEN_RSP_WORDS  3u
#define TFW_ELE_KEY_STORE_CLOSE_CMD_WORDS 2u
#define TFW_ELE_KEY_STORE_CLOSE_RSP_WORDS 2u

/* The flags this client sends (include/hsm/internal/hsm_key_store.h lines
 * 35 to 41): LOAD opens a store created before, CREATE makes a new one. No
 * other flag is sent; docs/multi-arch/imx95-design.md section 9.9 says why. */
#define TFW_ELE_KEY_STORE_LOAD   0x00u
#define TFW_ELE_KEY_STORE_CREATE 0x01u

/* KEY_MANAGEMENT_OPEN and KEY_MANAGEMENT_CLOSE (SAB_KEY_MANAGEMENT_OPEN_REQ,
 * SAB_KEY_MANAGEMENT_CLOSE_REQ; sab_msg_def.h lines 47 and 48). The
 * structures are sab_key_management.h's (src/common/sab_msg/include, lines
 * 11 to 35): the open request is the header, the key store handle, the input
 * and output address extensions, a word whose first byte is the flags and
 * whose others are reserved, and a CRC word; the open response is the header,
 * the status word and the key management handle; the close request is the
 * header and that handle, its response the header and the status word.
 * RM00284 section 3.2.6.5 and 3.2.6.6 (hsm_open_key_management_service,
 * hsm_close_key_management_service) is the library call over these
 * messages. */
#define TFW_ELE_KEY_MGMT_OPEN            0x40u
#define TFW_ELE_KEY_MGMT_CLOSE           0x41u
#define TFW_ELE_KEY_MGMT_OPEN_CMD_WORDS  6u
#define TFW_ELE_KEY_MGMT_OPEN_RSP_WORDS  3u
#define TFW_ELE_KEY_MGMT_CLOSE_CMD_WORDS 2u
#define TFW_ELE_KEY_MGMT_CLOSE_RSP_WORDS 2u

/* KEY_GENERATE (SAB_KEY_GENERATE_REQ, sab_msg_def.h line 49). The structures
 * are sab_key_generate.h's (src/common/sab_msg/include, lines 13 to 52) in
 * the PSA_COMPLIANT layout: the request is the header, the key management
 * handle, the key id, a word whose low half is the public key's size and
 * high half the key group, a word whose low half is the key type and high
 * half its size in bits, the lifetime, the usage, the permitted algorithm,
 * the key lifecycle, a word whose first byte is the flags and whose high
 * half is the private key's size, the public key buffer's address, and a CRC
 * word; the response is the header, the status word, the key id, and a word
 * whose low half is the public key size the enclave expected and high half
 * the private one's. hsm_generate_key, in RM00284 section 3.2's key
 * management group, is the library call over these messages. */
#define TFW_ELE_KEY_GENERATE           0x42u
#define TFW_ELE_KEY_GENERATE_CMD_WORDS 12u
#define TFW_ELE_KEY_GENERATE_RSP_WORDS 4u

/* The one key this client generates: NIST P-256, its public key the 64
 * bytes x then y (include/hsm/internal/hsm_key.h lines 112 and 173;
 * test/common/test_key_generate.c line 57), held in the enclave for the
 * life of its key store, never persistent (docs/multi-arch/imx95-design.md
 * section 9.11 says why). The usage bits and the algorithm are hsm_key.h
 * lines 50 and 54 and hsm_common_def.h line 35. */
#define TFW_ELE_KEY_TYPE_ECC_NIST     0x7112u       /* HSM_KEY_TYPE_ECC_NIST */
#define TFW_ELE_KEY_BITS_P256         256u          /* HSM_KEY_SIZE_ECC_NIST_256 */
#define TFW_ELE_P256_PUB_LEN          64u
#define TFW_ELE_LIFETIME_VOLATILE     0x00000000u   /* HSM_SE_KEY_STORAGE_VOLATILE */
#define TFW_ELE_LIFETIME_PERSISTENT   0x00000001u   /* HSM_SE_KEY_STORAGE_PERSISTENT */
#define TFW_ELE_USAGE_SIGN_HASH       0x00001000u   /* HSM_KEY_USAGE_SIGN_HASH */
#define TFW_ELE_USAGE_VERIFY_HASH     0x00002000u   /* HSM_KEY_USAGE_VERIFY_HASH */
#define TFW_ELE_ALGO_ECDSA_SHA256     0x06000609u   /* PERMITTED_ALGO_ECDSA_SHA256 */

/* SIGNATURE_GENERATION_OPEN and SIGNATURE_GENERATION_CLOSE
 * (SAB_SIGNATURE_GENERATION_OPEN_REQ, SAB_SIGNATURE_GENERATION_CLOSE_REQ;
 * sab_msg_def.h lines 75 and 76). The structures are sab_sign_gen.h's
 * (src/common/sab_msg/include, lines 11 to 35): the open request is the
 * header, the key store handle, the input and output address extensions, a
 * word whose first byte is the flags and whose others are reserved, and a
 * CRC word; the open response is the header, the status word and the
 * signature generation handle; the close request is the header and that
 * handle, its response the header and the status word. The PSA_COMPLIANT
 * library leaves the flags zero (src/common/sab_msg/sab_sign_gen.c lines 23
 * to 30). hsm_open_signature_generation_service and
 * hsm_close_signature_generation_service (include/hsm/internal/hsm_sign_gen.h
 * lines 49 and 61), in RM00284 section 3.2's signature generation group, are
 * the library calls over these messages. */
#define TFW_ELE_SIGN_GEN_OPEN            0x70u
#define TFW_ELE_SIGN_GEN_CLOSE           0x71u
#define TFW_ELE_SIGN_GEN_OPEN_CMD_WORDS  6u
#define TFW_ELE_SIGN_GEN_OPEN_RSP_WORDS  3u
#define TFW_ELE_SIGN_GEN_CLOSE_CMD_WORDS 2u
#define TFW_ELE_SIGN_GEN_CLOSE_RSP_WORDS 2u

/* SIGNATURE_GENERATE (SAB_SIGNATURE_GENERATE_REQ, sab_msg_def.h line 77).
 * The structures are sab_sign_gen.h's (lines 37 to 76) in the PSA_COMPLIANT
 * layout: the request is the header, the signature generation handle, the
 * key id, the input's address, the signature buffer's address, the input's
 * size, a word whose low half is the signature buffer's size, third byte the
 * flags and fourth reserved, the scheme id, a word whose low half is the salt
 * length and high half the key type, a word whose low half is the private
 * key's size and high half the key security size, the SM2 public key's and
 * identity's addresses, a word whose halves are their sizes, and a CRC word;
 * the response is the header, the status word, and a word whose low half is
 * the signature size the enclave expected. hsm_generate_signature
 * (hsm_sign_gen.h line 242) is the library call over these messages. */
#define TFW_ELE_SIGN_GENERATE           0x72u
#define TFW_ELE_SIGN_GENERATE_CMD_WORDS 14u
#define TFW_ELE_SIGN_GENERATE_RSP_WORDS 3u

/* The one scheme this client signs with: ECDSA over NIST P-256 with SHA-256,
 * the input the 32-byte digest, not the message (hsm_sign_gen.h lines 71 and
 * 108), the signature r then s, 64 bytes, with no Ry byte since no
 * compressed point is asked for (hsm_sign_gen.h lines 169 to 173). */
#define TFW_ELE_SIG_ECDSA_SHA256      0x06000609u   /* HSM_SIGNATURE_SCHEME_ECDSA_SHA256 */
#define TFW_ELE_SIGN_INPUT_DIGEST     0x00u         /* HSM_OP_GENERATE_SIGN_FLAGS_INPUT_DIGEST */
#define TFW_ELE_SHA256_LEN            32u
#define TFW_ELE_P256_SIG_LEN          64u

/* A request longer than four words ends in a CRC word, the XOR of every word
 * before it (sab_msg_def.h line 146, SAB_STORAGE_NB_WORDS_MAX_WO_CRC;
 * src/common/sab_msg/sab_process_msg.c lines 235 to 246; src/plat/ele/ele_utils.c lines 228 to
 * 250, plat_add_msg_crc). */
#define TFW_ELE_WORDS_WITHOUT_CRC 4u

/* The status word: bits 7 to 0 the status, 15 to 8 the rating
 * (GET_STATUS_CODE, GET_RATING_CODE). */
#define TFW_ELE_STATUS(w)  ((w) & 0xFFu)
#define TFW_ELE_RATING(w)  (((w) >> 8) & 0xFFu)
#define TFW_ELE_STATUS_OK   0xD6u   /* ELE_SUCCESS_IND */
#define TFW_ELE_STATUS_FAIL 0x29u   /* SAB_FAILURE_STATUS, ELE_FAILURE_IND */
#define TFW_ELE_RATING_UNKNOWN_HANDLE 0x07u   /* SAB_UNKNOWN_HANDLE, include/common/err.h line 25 */

/* The ELE MU's registers: PAR gives the transmit register count in bits 7
 * to 0 and the receive count in 15 to 8; TSR bit n is TR n empty, RSR bit n
 * RR n full; word k of a message goes through TR or RR k modulo the count. */
#define TFW_IMX95_MU_PAR 0x004u
#define TFW_IMX95_MU_TSR 0x124u
#define TFW_IMX95_MU_RSR 0x12Cu
#define TFW_IMX95_MU_TR  0x200u
#define TFW_IMX95_MU_RR  0x280u

/* How many times a status register is read for each word before the wait
 * gives up. A count, not a time: no clock is assumed. */
#define TFW_IMX95_ELE_POLLS 100000u

/* The longest message either way this client sends or takes, in words. */
#define TFW_ELE_MSG_WORDS 14u

/* The client's results. 0 is success; each refusal has its own. */
#define TFW_ELE_E_TIMEOUT 1   /* a register stayed empty or full past the bound */
#define TFW_ELE_E_MU      2   /* the MU reports no transmit or receive register */
#define TFW_ELE_E_LENGTH  3   /* a request longer than TFW_ELE_MSG_WORDS */
#define TFW_ELE_E_VERSION 4   /* the response's version is not 0x06 */
#define TFW_ELE_E_TAG     5   /* the response's tag is not a response's */
#define TFW_ELE_E_COMMAND 6   /* the response answers another command */
#define TFW_ELE_E_SIZE    7   /* the response's size is not the command's */
#define TFW_ELE_E_STATUS  8   /* the enclave answered, and not with success */
#define TFW_ELE_E_HANDLE  9   /* the enclave refused to close the handle */
#define TFW_ELE_E_SESSION 10  /* no session: handle 0, or the enclave knows it not */
#define TFW_ELE_E_KEY_STORE 11 /* no key store: handle 0, or the enclave knows it not */
#define TFW_ELE_E_KEY_MGMT  12 /* no key management service: handle 0, or unknown */
#define TFW_ELE_E_KEY_TYPE  13 /* a key or scheme this client does not use: type, size, lifetime or scheme */
#define TFW_ELE_E_BUFFER    14 /* the caller's buffer is too small for the public key or signature */
#define TFW_ELE_E_SIGN_GEN  15 /* no signature generation service: handle 0, or unknown */
#define TFW_ELE_E_SIGN_VER  16 /* no signature verification service: handle 0, or unknown */
#define TFW_ELE_E_SIG_BAD   17 /* the enclave verified the signature, and it does not hold */

struct tfw_ele_chan {
    uintptr_t mu;   /* the ELE MU's base as the M7 sees it */
};

struct tfw_ele_msg {
    uint32_t w[TFW_ELE_MSG_WORDS];   /* w[0] the header */
};

uint32_t tfw_ele_header(uint32_t command, uint32_t size, uint32_t tag);

/* Send `cmd` and read the response into `rsp`. The response must carry
 * version 0x06, the response tag, the command's id and `rsp_words` as its
 * size, and the status word must say success; `status`, when set, gets the
 * status word whenever one was read. Returns 0 or a TFW_ELE_E_ code. */
int32_t tfw_ele_call(const struct tfw_ele_chan *c, const struct tfw_ele_msg *cmd,
                     uint32_t rsp_words, struct tfw_ele_msg *rsp, uint32_t *status);

/* GET_INFO's request, for a buffer of TFW_ELE_GET_INFO_LEN bytes at
 * `buf_addr`, the address the enclave sees. */
void tfw_ele_get_info_cmd(struct tfw_ele_msg *m, uint64_t buf_addr);

/* GET_INFO's request and response, the buffer left to the parse below. */
int32_t tfw_ele_get_info(const struct tfw_ele_chan *c, uint64_t buf_addr, uint32_t *status);

/* SESSION_OPEN's request, naming the interrupt index of the caller's MU. */
void tfw_ele_session_open_cmd(struct tfw_ele_msg *m, uint8_t interrupt_idx);

/* SESSION_CLOSE's request for `handle`. */
void tfw_ele_session_close_cmd(struct tfw_ele_msg *m, uint32_t handle);

/* Open a session. On 0, `handle` gets the enclave's session handle, which
 * the caller keeps and hands to the close; on any refusal it is left as it
 * was. Returns 0 or a TFW_ELE_E_ code, as tfw_ele_call. */
int32_t tfw_ele_session_open(const struct tfw_ele_chan *c, uint8_t interrupt_idx,
                             uint32_t *handle, uint32_t *status);

/* Close the session `handle`. The enclave's failure status is
 * TFW_ELE_E_HANDLE, with the status word in `status`; every other refusal is
 * tfw_ele_call's. */
int32_t tfw_ele_session_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status);

/* KEY_STORE_OPEN's request: in the session `session`, the store `id` with
 * the nonce `nonce`, created when `create` is set and loaded when not, its
 * CRC word computed. */
void tfw_ele_key_store_open_cmd(struct tfw_ele_msg *m, uint32_t session, uint32_t id,
                                uint32_t nonce, uint8_t create);

/* KEY_STORE_CLOSE's request for `handle`. */
void tfw_ele_key_store_close_cmd(struct tfw_ele_msg *m, uint32_t handle);

/* Open a key store in the session `session`, which the caller opened and
 * keeps. Session handle 0 is TFW_ELE_E_SESSION unsent, as the library
 * refuses it (src/common/hsm_api/hsm_key_store.c line 54), and so is the
 * enclave's failure with the unknown-handle rating, a session closed. On 0,
 * `handle` gets the key store handle, which the caller keeps and hands to
 * the close; on any refusal it is left as it was. Every other refusal is
 * tfw_ele_call's. */
int32_t tfw_ele_key_store_open(const struct tfw_ele_chan *c, uint32_t session, uint32_t id,
                               uint32_t nonce, uint8_t create, uint32_t *handle,
                               uint32_t *status);

/* Close the key store `handle`. The enclave's failure status is
 * TFW_ELE_E_HANDLE, with the status word in `status`; every other refusal is
 * tfw_ele_call's. */
int32_t tfw_ele_key_store_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status);

/* KEY_MANAGEMENT_OPEN's request: on the key store `store`, the address
 * extensions and flags zero, as the library sends them (its message buffer
 * is zeroed, src/common/sab_msg/sab_process_msg.c line 174, and
 * sab_key_management.c sets only the handle and flags, which its callers
 * leave zero), its CRC word computed. */
void tfw_ele_key_mgmt_open_cmd(struct tfw_ele_msg *m, uint32_t store);

/* KEY_MANAGEMENT_CLOSE's request for `handle`. */
void tfw_ele_key_mgmt_close_cmd(struct tfw_ele_msg *m, uint32_t handle);

/* Open the key management service on the key store `store`, which the
 * caller opened and keeps. Key store handle 0 is TFW_ELE_E_KEY_STORE unsent,
 * as the library refuses it (src/common/hsm_api/hsm_key_management.c line
 * 28), and so is the enclave's failure with the unknown-handle rating, a
 * store closed. On 0, `handle` gets the key management handle, which the
 * caller keeps and hands to the close; on any refusal it is left as it was.
 * Every other refusal is tfw_ele_call's. */
int32_t tfw_ele_key_mgmt_open(const struct tfw_ele_chan *c, uint32_t store, uint32_t *handle,
                              uint32_t *status);

/* Close the key management service `handle`. The enclave's failure status
 * is TFW_ELE_E_HANDLE, with the status word in `status`; every other refusal
 * is tfw_ele_call's. */
int32_t tfw_ele_key_mgmt_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status);

/* What KEY_GENERATE is asked for. The public key buffer is the caller's:
 * `pub_addr` is its address as the enclave sees it and `pub_size` its size
 * in bytes, and the enclave writes the public key there. */
struct tfw_ele_key_gen {
    uint32_t key_id;      /* the id asked for; 0 lets the enclave choose */
    uint32_t key_type;    /* TFW_ELE_KEY_TYPE_ECC_NIST */
    uint32_t lifetime;    /* TFW_ELE_LIFETIME_VOLATILE */
    uint32_t usage;       /* TFW_ELE_USAGE_ bits */
    uint32_t algo;        /* the permitted algorithm */
    uint16_t group;       /* the key group, 0 to 99 */
    uint16_t pub_size;    /* at least TFW_ELE_P256_PUB_LEN */
    uint32_t pub_addr;
};

/* KEY_GENERATE's request: on the key management service `mgmt`, the key
 * `k` describes, a P-256 key's size in bits, lifecycle, flags and private key
 * size zero, its CRC word computed. */
void tfw_ele_key_generate_cmd(struct tfw_ele_msg *m, uint32_t mgmt, const struct tfw_ele_key_gen *k);

/* Generate the key `k` describes on the key management service `mgmt`,
 * which the caller opened and keeps. Refused unsent: key management handle
 * 0, TFW_ELE_E_KEY_MGMT, as the library refuses it
 * (src/common/hsm_api/hsm_key_generate.c line 36); a key type other than
 * NIST P-256 or a lifetime other than volatile, TFW_ELE_E_KEY_TYPE; a buffer
 * smaller than TFW_ELE_P256_PUB_LEN, TFW_ELE_E_BUFFER. The enclave's failure
 * with the unknown-handle rating, a service closed, is TFW_ELE_E_KEY_MGMT.
 * On 0, `key_id` gets the enclave's key id and the public key is in the
 * caller's buffer; on any refusal `key_id` is left as it was. Every other
 * refusal is tfw_ele_call's. */
int32_t tfw_ele_key_generate(const struct tfw_ele_chan *c, uint32_t mgmt,
                             const struct tfw_ele_key_gen *k, uint32_t *key_id,
                             uint32_t *status);

/* SIGNATURE_GENERATION_OPEN's request on the key store `store`, the flags
 * and address extensions zero, its CRC word computed; CLOSE's on `handle`. */
void tfw_ele_sign_gen_open_cmd(struct tfw_ele_msg *m, uint32_t store);
void tfw_ele_sign_gen_close_cmd(struct tfw_ele_msg *m, uint32_t handle);

/* Open the signature generation service on the key store `store`, which the
 * caller opened and keeps. A key store handle of 0 is refused unsent,
 * TFW_ELE_E_KEY_STORE, as the library refuses it
 * (src/common/hsm_api/hsm_sign_gen.c line 31); so is the enclave's failure
 * with the unknown-handle rating, a store closed. On 0, `handle` gets the
 * service's handle; on any refusal it is left as it was. */
int32_t tfw_ele_sign_gen_open(const struct tfw_ele_chan *c, uint32_t store, uint32_t *handle,
                              uint32_t *status);

/* Close the signature generation service `handle`. The enclave's refusal,
 * a handle it does not know, is TFW_ELE_E_HANDLE. */
int32_t tfw_ele_sign_gen_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status);

/* One signature: the key the enclave holds, as the caller generated it, and
 * the caller's two buffers by the address the enclave sees. The HAL keeps
 * none of it. */
struct tfw_ele_sign {
    uint32_t key_id;       /* the id KEY_GENERATE returned */
    uint32_t key_type;     /* the key's type as generated: TFW_ELE_KEY_TYPE_ECC_NIST */
    uint32_t key_bits;     /* its size: TFW_ELE_KEY_BITS_P256 */
    uint32_t scheme;       /* TFW_ELE_SIG_ECDSA_SHA256 */
    uint32_t digest_addr;  /* the caller's TFW_ELE_SHA256_LEN-byte digest */
    uint32_t sig_addr;     /* the caller's buffer for r||s */
    uint16_t sig_size;     /* at least TFW_ELE_P256_SIG_LEN */
};

/* SIGNATURE_GENERATE's request: on the signature generation service `sgen`,
 * the signature `s` describes over a SHA-256 digest, salt, key type, private
 * key, SM2 fields and flags zero, its CRC word computed. */
void tfw_ele_sign_generate_cmd(struct tfw_ele_msg *m, uint32_t sgen, const struct tfw_ele_sign *s);

/* Sign the digest `s` names on the signature generation service `sgen`,
 * which the caller opened and keeps. Refused unsent: service handle 0,
 * TFW_ELE_E_SIGN_GEN, as the library refuses it (hsm_sign_gen.c line 129);
 * a key other than NIST P-256 or a scheme other than ECDSA with SHA-256,
 * TFW_ELE_E_KEY_TYPE; a buffer smaller than TFW_ELE_P256_SIG_LEN,
 * TFW_ELE_E_BUFFER. The enclave's failure with the unknown-handle rating, a
 * service closed, is TFW_ELE_E_SIGN_GEN. On 0 the signature r||s is in the
 * caller's buffer. Every other refusal is tfw_ele_call's. */
int32_t tfw_ele_sign_generate(const struct tfw_ele_chan *c, uint32_t sgen,
                              const struct tfw_ele_sign *s, uint32_t *status);

/* PUB_KEY_EXPORT, the library's SAB_PUB_KEY_RECOVERY_REQ (sab_msg_def.h line
 * 44; "Public Key Recovery is now also known as Public Key Exportation",
 * hsm_key_recovery.h lines 16 to 18), on the key store handle. PSA_COMPLIANT
 * layout (sab_key_recovery.h lines 13 to 36): the request is the header, the
 * key store handle, the key id, the output address extension, the output
 * address, the output size and a reserved half-word, the CRC word; the
 * response the header, the status, the size the enclave expected and a
 * reserved half-word. The key comes out as X then Y, 64 bytes for P-256
 * (test/common/test_key_recovery.c line 14). */
#define TFW_ELE_PUB_KEY_EXPORT           0x32u
#define TFW_ELE_PUB_KEY_EXPORT_CMD_WORDS 7u
#define TFW_ELE_PUB_KEY_EXPORT_RSP_WORDS 3u

/* One public key to export. The PSA_COMPLIANT request carries no key type,
 * so the caller passes the type and size it generated the key with. */
struct tfw_ele_key_export {
    uint32_t key_id;       /* the id KEY_GENERATE returned */
    uint32_t key_type;     /* TFW_ELE_KEY_TYPE_ECC_NIST */
    uint32_t key_bits;     /* TFW_ELE_KEY_BITS_P256 */
    uint32_t out_addr;     /* the caller's buffer for X||Y */
    uint16_t out_size;     /* at least TFW_ELE_P256_PUB_LEN */
};

/* PUB_KEY_EXPORT's request: on the key store `store`, the key `x` names, the
 * address extension zero, its CRC word computed. */
void tfw_ele_pub_key_export_cmd(struct tfw_ele_msg *m, uint32_t store,
                                const struct tfw_ele_key_export *x);

/* Export the public key `x` names from the key store `store`, which the
 * caller opened and keeps. Refused unsent: store handle 0,
 * TFW_ELE_E_KEY_STORE, as the library refuses it (hsm_key_recovery.c line
 * 31); a key other than NIST P-256, TFW_ELE_E_KEY_TYPE; a buffer smaller than
 * TFW_ELE_P256_PUB_LEN, TFW_ELE_E_BUFFER. The enclave's failure with the
 * unknown-handle rating, a store closed, is TFW_ELE_E_KEY_STORE. On 0 X||Y is
 * in the caller's buffer. Every other refusal is tfw_ele_call's. */
int32_t tfw_ele_pub_key_export(const struct tfw_ele_chan *c, uint32_t store,
                               const struct tfw_ele_key_export *x, uint32_t *status);

/* The signature verification service (sab_msg_def.h lines 81 to 83), opened
 * on the session: hsm_open_signature_verification_service takes a session or
 * a key store handle (hsm_verify_sign.c lines 37 to 48) and sends it as
 * session_handle. PSA_COMPLIANT layout (sab_verify_sign.h lines 11 to 74):
 * the open request is the header, the session handle, the input and output
 * address extensions, the flags and three reserved bytes, the CRC word; its
 * response the header, the status, the service's handle. The close request
 * is the header and that handle, its response the header and the status. */
#define TFW_ELE_SIGN_VER_OPEN            0x80u
#define TFW_ELE_SIGN_VER_CLOSE           0x81u
#define TFW_ELE_SIGN_VER_OPEN_CMD_WORDS  6u
#define TFW_ELE_SIGN_VER_OPEN_RSP_WORDS  3u
#define TFW_ELE_SIGN_VER_CLOSE_CMD_WORDS 2u
#define TFW_ELE_SIGN_VER_CLOSE_RSP_WORDS 2u

/* SIGNATURE_VERIFICATION_OPEN's request on the session `session`, the
 * address extensions and flags zero, its CRC word computed; CLOSE's on the
 * service `handle`. */
void tfw_ele_sign_ver_open_cmd(struct tfw_ele_msg *m, uint32_t session);
void tfw_ele_sign_ver_close_cmd(struct tfw_ele_msg *m, uint32_t handle);

/* Open the verification service on the session `session`. Session 0 is
 * TFW_ELE_E_SESSION unsent, as the library refuses it (hsm_verify_sign.c line
 * 32); the enclave's failure with the unknown-handle rating is
 * TFW_ELE_E_SESSION too. On 0 `*handle` is the service's handle; on any
 * refusal it is left as it was. */
int32_t tfw_ele_sign_ver_open(const struct tfw_ele_chan *c, uint32_t session, uint32_t *handle,
                              uint32_t *status);

/* Close the verification service `handle`. The enclave's refusal is
 * TFW_ELE_E_HANDLE. */
int32_t tfw_ele_sign_ver_close(const struct tfw_ele_chan *c, uint32_t handle, uint32_t *status);

/* SIGNATURE_VERIFY (sab_msg_def.h line 83; sab_verify_sign.h lines 37 to 74,
 * PSA_COMPLIANT): fourteen words, the header; the service's handle; the
 * public key's address; the input's address; the signature's address; the
 * input's size; the signature's size and the key's size; the key security
 * size and the public key type; the flags and three reserved bytes; the
 * scheme; the salt length and a reserved half-word; the SM2 identity's
 * address; its size and a reserved half-word; the CRC word. The response is
 * the header, the status and the verification status. */
#define TFW_ELE_SIGN_VERIFY           0x82u
#define TFW_ELE_SIGN_VERIFY_CMD_WORDS 14u
#define TFW_ELE_SIGN_VERIFY_RSP_WORDS 3u

/* include/hsm/internal/hsm_key.h line 91, hsm_verify_sign.h lines 165 and
 * 191, lines 204 and 209 */
#define TFW_ELE_PUBKEY_TYPE_ECC_NIST  0x4112u       /* HSM_PUBKEY_TYPE_ECC_NIST */
#define TFW_ELE_VERIFY_INPUT_DIGEST   0x00u         /* INPUT_DIGEST, PLAINTEXT_KEY */
#define TFW_ELE_VERIFY_PASSED         0x5A3CC3A5u   /* HSM_VERIFICATION_STATUS_SUCCESS */
#define TFW_ELE_VERIFY_FAILED         0x2B4DD4B2u   /* HSM_VERIFICATION_STATUS_FAILURE */

/* One signature to verify against a plaintext public key, as
 * tfw_ele_pub_key_export writes it. */
struct tfw_ele_verify {
    uint32_t key_type;     /* the key's type: TFW_ELE_KEY_TYPE_ECC_NIST */
    uint32_t key_bits;     /* its size: TFW_ELE_KEY_BITS_P256 */
    uint32_t scheme;       /* TFW_ELE_SIG_ECDSA_SHA256 */
    uint32_t key_addr;     /* the caller's X||Y */
    uint32_t digest_addr;  /* the caller's TFW_ELE_SHA256_LEN-byte digest */
    uint32_t sig_addr;     /* the caller's r||s */
    uint16_t key_size;     /* at least TFW_ELE_P256_PUB_LEN */
    uint16_t sig_size;     /* at least TFW_ELE_P256_SIG_LEN */
};

/* SIGNATURE_VERIFY's request: on the verification service `sver`, the
 * signature `v` describes over a SHA-256 digest, a 64-byte plaintext NIST
 * P-256 key, flags, salt and SM2 fields zero, its CRC word computed. */
void tfw_ele_sign_verify_cmd(struct tfw_ele_msg *m, uint32_t sver, const struct tfw_ele_verify *v);

/* Verify the signature `v` names on the verification service `sver`, which
 * the caller opened and keeps. Refused unsent: service handle 0,
 * TFW_ELE_E_SIGN_VER, as the library refuses it (hsm_verify_sign.c line
 * 134); a key other than NIST P-256 or a scheme other than ECDSA with
 * SHA-256, TFW_ELE_E_KEY_TYPE; a key or signature shorter than 64 bytes,
 * TFW_ELE_E_BUFFER. The enclave's failure with the unknown-handle rating is
 * TFW_ELE_E_SIGN_VER. A response whose verification status is anything but
 * TFW_ELE_VERIFY_PASSED is TFW_ELE_E_SIG_BAD, as the library reads it
 * (hsm_verify_sign.c lines 173 to 177), with that word in `*verdict` if not
 * NULL. 0 is a signature that holds. Every other refusal is tfw_ele_call's. */
int32_t tfw_ele_sign_verify(const struct tfw_ele_chan *c, uint32_t sver,
                            const struct tfw_ele_verify *v, uint32_t *verdict, uint32_t *status);

/* The fields of the information buffer this port reads (sab_dev_getinfo.h
 * struct dev_info and struct dev_addn_info, little-endian, no padding). */
struct tfw_ele_info {
    uint16_t soc_id;
    uint16_t soc_rev;
    uint16_t lmda;          /* the lifecycle, RM00284 section 3.13.2.2 */
    uint8_t  ssm_state;
    uint8_t  uid[16];
    uint8_t  sha_fw[32];    /* the first 256 bits of the firmware's SHA */
    uint8_t  trng_state;
    uint8_t  csal_state;
    uint8_t  imem_state;
};

void tfw_ele_get_info_parse(const uint8_t buf[TFW_ELE_GET_INFO_LEN], struct tfw_ele_info *out);

/* Where the fields the device attestation reports sit in the information
 * buffer (RM00284 section 3.12 lists them; section 3.13 returns them
 * unsigned): byte offset and length, the ones tfw_ele_get_info_parse reads
 * in the struct dev_info and struct dev_addn_info layout. The ROM patch hash,
 * the OEM SRK hash and the committed SRK revocation bits are not here: no
 * source this project cites places them in the buffer (board_profile.h). */
#define TFW_ELE_INFO_LMDA_OFF       8u
#define TFW_ELE_INFO_LMDA_LEN       2u
#define TFW_ELE_INFO_SHA_FW_OFF     60u
#define TFW_ELE_INFO_SHA_FW_LEN     32u

/* hsm_delete_key's strict-operation flag, bit 7 (RM00284 section 3.2.6.1,
 * HSM_OP_DEL_KEY_FLAGS_STRICT_OPERATION; section 3.2.3.1): the enclave
 * writes the key store to NVM before it acknowledges, so the delete survives
 * a reset (section 2.4.2). */
#define TFW_ELE_DEL_KEY_STRICT 0x80u

/* The client's refusal of a message no source this project holds lays out:
 * its command id is 0, or its words do not fit. */
#define TFW_ELE_E_LAYOUT 18

/* The delete-key message's id and layout. RM00284 gives the call and its
 * flag, not the message: no document this project holds names the id or
 * where the handle, the key id and the flags sit, so the caller passes them
 * (board_profile.h, all zero until a source names them). Words are indices
 * into the request, the header being word 0; the flags are shifted left by
 * `flags_shift` within their word. A request longer than four words ends in
 * the CRC word, as every other. */
struct tfw_ele_del_layout {
    uint32_t command;      /* the message id; 0 is unknown */
    uint8_t  cmd_words;    /* the request's size in words, the header counted */
    uint8_t  rsp_words;    /* the response's */
    uint8_t  w_handle;     /* the key management handle's word */
    uint8_t  w_key;        /* the key id's word */
    uint8_t  w_flags;      /* the flags' word */
    uint8_t  flags_shift;  /* the flags' bit offset in it */
};

/* The delete-key request `l` lays out: on the key management service `mgmt`,
 * the key `key_id`, the strict-operation flag always set. 0, or
 * TFW_ELE_E_LAYOUT, unbuilt, when the layout's id is 0, its sizes exceed
 * TFW_ELE_MSG_WORDS or are under two words, or a field's word is the header
 * or past the request (or the CRC word). */
int32_t tfw_ele_key_delete_cmd(struct tfw_ele_msg *m, const struct tfw_ele_del_layout *l,
                               uint32_t mgmt, uint32_t key_id);

/* Delete the key `key_id` on the key management service `mgmt`, a strict
 * operation. 0 only once the enclave acknowledges it with success: only
 * then is the delete committed to NVM. Refused unsent: handle 0,
 * TFW_ELE_E_KEY_MGMT; a layout tfw_ele_key_delete_cmd refuses,
 * TFW_ELE_E_LAYOUT. The enclave's failure with the unknown-handle rating is
 * TFW_ELE_E_KEY_MGMT; every other refusal, a timeout among them, is
 * tfw_ele_call's.
 *
 * Keys only. Deleting a key group is not offered: RM00284 does not say how to
 * request it (docs/hsm-self-defence/engineering.md slice 6; architecture
 * question 5). */
int32_t tfw_ele_key_delete(const struct tfw_ele_chan *c, const struct tfw_ele_del_layout *l,
                           uint32_t mgmt, uint32_t key_id, uint32_t *status);

/* hsm_get_event (#50 slice 10, S2 by polling: architecture question 6 (a),
 * unconfirmed until the slice 10 bench test). No document this project holds
 * gives the message's id, its words or the event codes it returns (open:
 * IMX95RM, imx-secure-enclave), so each is the board's, zero until known, and
 * a zero refuses the call unsent. The response holds at most
 * TFW_ELE_EVENTS_MAX events (architecture.md question 6). */
#define TFW_ELE_EVENTS_MAX 8u

struct tfw_ele_event_layout {
    uint32_t command;    /* the message id; 0 is unknown */
    uint8_t  cmd_words;  /* the request's size in words, the header counted */
    uint8_t  rsp_words;  /* the response's */
    uint8_t  w_session;  /* the request's session handle word */
    uint8_t  w_count;    /* the response's event count word */
    uint8_t  w_first;    /* the response's first event word */
};

/* Build the request on session `session`. TFW_ELE_E_LAYOUT, unbuilt, for an
 * unknown id, a size past TFW_ELE_MSG_WORDS, a word index of 0 or on the CRC
 * word, or a response with no room for its count and one event. */
int32_t tfw_ele_get_event_cmd(struct tfw_ele_msg *m, const struct tfw_ele_event_layout *l,
                              uint32_t session);

/* Poll the enclave's events on `session` into `ev`, at most `max` and never
 * past TFW_ELE_EVENTS_MAX or the response's last word; *n is how many. Refused
 * unsent: session 0, TFW_ELE_E_SESSION; a layout tfw_ele_get_event_cmd
 * refuses, TFW_ELE_E_LAYOUT. Every other refusal is tfw_ele_call's. */
int32_t tfw_ele_get_event(const struct tfw_ele_chan *c, const struct tfw_ele_event_layout *l,
                          uint32_t session, uint32_t *ev, uint32_t max, uint32_t *n,
                          uint32_t *status);

/* hsm_dev_attest's nonce: 4 or 16 bytes, the caller's (RM00284 section 3.12;
 * docs/hsm-self-defence/architecture.md question 17). */
#define TFW_ELE_ATTEST_NONCE_SHORT 4u
#define TFW_ELE_ATTEST_NONCE_LONG  16u

/* The client's refusal of a nonce of any other length. */
#define TFW_ELE_E_NONCE 19

/* The device attestation message's id and layout. RM00284 section 3.12 gives
 * the call, its nonce and the fields it reports, not the message: no
 * document this project holds names the id, where the session, the nonce and
 * the report buffer sit, or the report's size and layout (architecture
 * question 17), so the caller passes them (board_profile.h, all zero until a
 * source names them). Words are indices into the request, the header being
 * word 0. The nonce takes its words from `w_nonce` on, byte 0 in bits 7:0 of
 * the first, the order the M7's memory holds it; that order is open with the
 * rest of the layout. `w_session` 0 is a message that carries no session. A
 * request longer than four words ends in the CRC word, as every other. */
struct tfw_ele_attest_layout {
    uint32_t command;      /* the message id; 0 is unknown */
    uint8_t  cmd_words;    /* the request's size in words, the header counted */
    uint8_t  rsp_words;    /* the response's */
    uint8_t  w_session;    /* the session handle's word; 0, none */
    uint8_t  w_nonce;      /* the nonce's first word */
    uint8_t  w_addr_hi;    /* the report buffer's address, bits 63:32 */
    uint8_t  w_addr_lo;    /* bits 31:0 */
    uint8_t  w_size;       /* the report buffer's size in bytes */
    uint16_t report_len;   /* the report's size in bytes; 0 is unknown */
};

/* The device attestation request `l` lays out: on the session `session`,
 * under the nonce `nonce` of `nonce_len` bytes, the report written to the
 * buffer at `buf_addr`, `l->report_len` bytes. 0; TFW_ELE_E_NONCE, unbuilt,
 * for a nonce other than 4 or 16 bytes; TFW_ELE_E_LAYOUT, unbuilt, when the
 * layout's id or report size is 0, its sizes exceed TFW_ELE_MSG_WORDS or are
 * under two words, or a field's word is the header or past the request (or
 * the CRC word). */
int32_t tfw_ele_dev_attest_cmd(struct tfw_ele_msg *m, const struct tfw_ele_attest_layout *l,
                               uint32_t session, const uint8_t *nonce, uint32_t nonce_len,
                               uint64_t buf_addr);

/* Ask the enclave for its device attestation into the buffer at `buf_addr`,
 * as the enclave sees it, of `buf_len` bytes. Refused unsent: a layout that
 * carries the session with session 0, TFW_ELE_E_SESSION, as the library
 * refuses it; a buffer shorter than `l->report_len`, TFW_ELE_E_BUFFER; what
 * tfw_ele_dev_attest_cmd refuses. The enclave's failure with the
 * unknown-handle rating is TFW_ELE_E_SESSION; every other refusal, a timeout
 * among them, is tfw_ele_call's.
 *
 * The report is not verified here: its signing key and certificate chain are
 * open (architecture question 17), and appraising it is go-xkms's. */
int32_t tfw_ele_dev_attest(const struct tfw_ele_chan *c, const struct tfw_ele_attest_layout *l,
                           uint32_t session, const uint8_t *nonce, uint32_t nonce_len,
                           uint64_t buf_addr, uint32_t buf_len, uint32_t *status);

#endif /* TFW_CM_IMX95_ELE_MU_H */
