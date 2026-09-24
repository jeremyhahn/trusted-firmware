# DICE attestation — CLI

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## `agctl` has no `attest` or `attest-cert` verb

`sdk/go/cmd/agctl/main.go`'s command switch is `ping`, `caps`, `pin`, `sys`,
`reset`, `mode`, `read`, `write`, `timer`, `timer-cancel`, `timer-query`,
`ports`. `ATTEST` and `ATTEST_CERT` are reachable only through the Go SDK
(`Device.Attest`, `Device.AttestCert` in `sdk/go/attest.go`) — there is no
CLI verb for either command today. This is a gap, not a documented design
choice; file it against `agctl`, do not work around it by inventing flags
here.

## `agctl caps` — see the root of trust before trusting anything it says

```
agctl [-baud N] [-timeout D] [-json] <port> caps
```

Relevant fields of the text output (`sdk/go/cmd/agctl/main.go`):

```
root of trust SOFTWARE           # or NONE / SECURE_ELEMENT / TPM2 — Caps.CryptoTier
algorithms    ECDSA_P256_SHA256|HMAC_SHA256   # Caps.Algorithms, what the provider can actually sign with
features      ... attest ...     # present only when AG_FEATURE_ATTEST=1
```

Example:

```
agctl /dev/ttyACM0 caps
agctl -json /dev/ttyACM0 caps | jq .CryptoTier
```

## `agprov` — provisions the element that DICE identity depends on

`ATTEST`/`ATTEST_CERT` need a DeviceID key and a UDS in place first;
`agprov` is the tool that puts them there, on a **provisioning** image only
(`AG_FEATURE_PROVISION=1`; a production image answers `AG_ERR_UNKNOWN_CMD`
for every verb below). Full procedure in `docs/device-provisioning/user.md`;
only the flags are repeated here.

```
agprov [flags] <port>
```

| Flag | Meaning |
|---|---|
| `--dry-run` | print the plan (config blocks, DeviceID `GENKEY`, `SEED_UDS`, both zone locks); write nothing |
| `--verify` | read the part back and compare against `--config`; safe to run on a locked part |
| `--commit` | run the irreversible sequence; requires `--yes-i-mean-it` |
| `--yes-i-mean-it` | required by `--commit` |
| `--config PATH` | the provisioning template (YAML); required in every mode |
| `--baud N` | serial baud rate |
| `--timeout D` | reply timeout |
| `--force` | refused outright — not accepted as a synonym for `--yes-i-mean-it` |

Exactly one of `--dry-run` / `--verify` / `--commit` is required.

Examples:

```
agprov --dry-run --config board.yaml /dev/ttyACM0
agprov --commit --yes-i-mean-it --config board.yaml /dev/ttyACM0
agprov --verify --config board.yaml /dev/ttyACM0
```

`--commit` runs, in the one order the element allows: write config blocks,
lock the config zone, generate the DeviceID key in the element (`GENKEY`,
slot 0 — see `ag.SlotDeviceID`), seed the UDS from the element's TRNG
(`SEED_UDS`, slot 1 — `ag.SlotUDS`), lock the data zone. Every step is
irreversible from the second one on; run `--commit` on a spare part first.
