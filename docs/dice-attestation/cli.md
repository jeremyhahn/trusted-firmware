# DICE attestation — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-21
> Conflicts with this document are defects. Fix them here first.

## `tfwctl attest` and `tfwctl attest-cert`

```
tfwctl [-baud N] [-timeout D] [-boot-wait D] [-json] <port> attest      <16 hex bytes>
tfwctl [-baud N] [-timeout D] [-boot-wait D] [-json] <port> attest-cert <32 hex bytes>
```

`-boot-wait` is how long tfwctl waits for the firmware's BOOT event before the
first command (default `tfw.DefaultBootWait`, 0 disables it); raise it for a
`BOOT_MEASURE=atecc608a` image, which can take seconds to measure a large
application. `<port>` is a serial port, `tcp://host:port` or `unix:<path>`
(`sdk/go/cmd/tfwctl/main.go`). With `tcp://` tfwctl listens there, the
endpoint in the device's network block, and runs the command on the first
device to connect within `-timeout`; `unix:<path>` is an emulator's UART on a
Unix socket (QEMU `-serial unix:<path>,server=on`). Over Ethernet the device
answers only CAPS, APP_INFO, ATTEST, ATTEST_CERT, ATTEST_CSR and OTA staging
(OTA_BEGIN, OTA_DATA, OTA_END, OTA_STATUS), the list in
`src/proto/link_eth.h` `tfw_link_eth_allows`, so every verb on this page works
over it and `tfwprov` does not.

`attest` prints the 164-byte evidence frame as hex, and with `-json`
`{"evidence": <hex>, "len": 164}`. `attest-cert` prints the alias certificate
as DER hex, and with `-json` `{"certificate": <hex>, "len": <n>,
"app_evidence": <hex>}`, where `app_evidence` is the 32 bytes of the
certificate's measurements claim: the running application's evidence, or the
firmware measurement where it supplied none (`tfw.ApplicationEvidence`,
[the application's evidence](../application-sdk/user.md#put-your-evidence-in-the-devices-attestation)).
Both are `sdk/go/cmd/tfwctl/peripherals.go` over
`Device.Attest` and `Device.AttestCert`.

**The nonce is the caller's and the tool never generates one.** A client that
invents its own challenge is a client attesting to itself: freshness has to
come from whoever is deciding whether to trust the answer. A nonce of the
wrong length is refused before a frame goes out — the two lengths differ
because they answer different questions, and are documented in `api.md`.

## `tfwctl attest-csr`

```
tfwctl [-baud N] [-timeout D] [-boot-wait D] [-json] <port> attest-csr --nonce <32 hex bytes>
```

Prints the 64-byte `r‖s` signature the device's DeviceID key made for that
challenge, as 128 hex digits and a newline. With `-json` it prints
`{"signature": <hex>, "len": 64}` instead. It is `sdk/go/cmd/tfwctl/attestcsr.go`
over `Device.AttestCSR`.

The signature is all the device returns. The `CertificationRequest` around it
(the CertificationRequestInfo, the DER wrapping, the check against the DeviceID
key) is certificate-profile work and go-xkms's, so this verb does not build one.

```
tfwctl /dev/ttyACM0 attest-csr --nonce "$(printf %s "$KEY_AUTH" | sha256sum | cut -d' ' -f1)"
```

The nonce is the caller's, as for `attest-cert`, and is a flag here, as the verb
shipped. The flag is required; a missing, empty, non-hex or
wrong-length nonce, an unknown flag or a stray argument is refused before a frame
goes out, and nothing is written to stdout on any failure. A device that does not
offer the command fails with `TFW_ERR_UNSUPPORTED`; `tfwctl caps` lists
`attest-csr` among the features when it does.

## `tfwctl caps` — see the root of trust before trusting anything it says

```
tfwctl [-baud N] [-timeout D] [-boot-wait D] [-json] <port> caps
```

The text output (`sdk/go/cmd/tfwctl/main.go`) is protocol, firmware, board
id, pins, sched slots and max payload, then these three lines in this order:

```
root of trust SOFTWARE         # or NONE / SECURE_ELEMENT / TPM2 — Caps.CryptoTier
features    ... attest ...     # present only when TFW_FEATURE_ATTEST=1
algorithms  ECDSA_P256_SHA256|HMAC_SHA256   # Caps.Algorithms, what the provider can actually sign with
```

Example:

```
tfwctl /dev/ttyACM0 caps
tfwctl -json /dev/ttyACM0 caps | jq .CryptoTier
```

## `tfwprov` — provisions the element that DICE identity depends on

`ATTEST`/`ATTEST_CERT` need a DeviceID key and a UDS in place first;
`tfwprov` is the tool that puts them there, on a **provisioning** image only
(`TFW_FEATURE_PROVISION=1`; a production image answers `TFW_ERR_UNKNOWN_CMD`
for every verb below). Full procedure in `docs/device-provisioning/user.md`;
only the flags are repeated here.

```
tfwprov [flags] <port>
```

| Flag | Meaning |
|---|---|
| `--dry-run` | print the plan (config blocks, DeviceID `GENKEY`, `SEED_UDS`, both zone locks); write nothing |
| `--verify` | read the part back and compare against `--config`; safe to run on a locked part |
| `--commit` | run the irreversible sequence; requires `--yes-i-mean-it` |
| `--yes-i-mean-it` | required by `--commit` |
| `--config PATH` | the provisioning template, a `key = value` file (`sdk/go/templates/*.conf`), not YAML; required in each of these three modes |
| `--baud N` | serial baud rate |
| `--timeout D` | reply timeout; default 30s |
| `--force` | refused outright — not accepted as a synonym for `--yes-i-mean-it` |
| `--record PATH` | `--commit`/`--verify`: join the DeviceID point to the provisioning record `--bake-eeprom --record` started: the UEID (RFC 9711 §4.2.1 `ueid`) and the point as an RFC 7800 §3.2 `cnf` JWK. A different point is refused, never overwritten ([the provisioning record](../device-provisioning/cli.md#the-provisioning-record)) |

Exactly one of `--dry-run` / `--verify` / `--commit` is required, except in the
separate modes `--bake-eeprom`, `--bake-netblock` and `--write-pubkey`, which
take none of them and no `--config` ([the provisioning CLI](../device-provisioning/cli.md)).

Examples:

```
tfwprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyACM0
tfwprov --commit --yes-i-mean-it --config sdk/go/templates/atecc608a-dice.conf /dev/ttyACM0
tfwprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyACM0
```

`--commit` runs, in the one order the element allows: write config blocks,
lock the config zone, generate the DeviceID key in the element (`GENKEY`,
slot 0 — see `tfw.SlotDeviceID`), seed the UDS from the element's TRNG
(`SEED_UDS`, slot 1 — `tfw.SlotUDS`), lock the data zone. Every step is
irreversible from the second one on; run `--commit` on a spare part first.
