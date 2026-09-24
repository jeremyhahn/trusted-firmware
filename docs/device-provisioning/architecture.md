# Device provisioning — architecture

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Provisioning is what turns a blank part into a device with an identity: fuses
and lock bits on the MCU, the ATECC608A secure element's configuration and
locking, and the device serial. It happens once, on a bench or a jig, never
over the network and never from the production firmware.

See also: [`engineering.md`](engineering.md) (exported surface, invariants,
tests), [`cli.md`](cli.md) (`agprov` verbs and flags), [`api.md`](api.md)
(wire commands), [`user.md`](user.md) (the walkthrough).

## Component boundaries

| Component | Owns | Cannot |
|---|---|---|
| ISP programmer (`avrdude`) | fuses, lock bits | touch the secure element or EEPROM |
| `agprov` (`sdk/go/cmd/agprov`) | drives the ATECC608A over the wire protocol | set fuses, set lock bits, or write the platform-reserved EEPROM region |
| `src/caps/provision.c` | the six `PROV_*` command handlers | exist in a production build — `AG_FEATURE_PROVISION` defaults to 0 and the file is not compiled in |
| `sdk/go/provision.go` | typed host calls (`Device.Prov*`) over `sdk/go` | generate or read back any secret; the element refuses reading a never-readable slot |
| the provisioning template (`sdk/go/templates/*.conf`) | slot policy for the config zone | be interpreted by the firmware — the firmware writes the block it is given and has no opinion about policy |

Three independent mechanisms, three independent tools. None of them can undo
another's step, and none of them is gated behind a shared "provisioning mode" —
each enforces its own irreversibility separately (see below).

## Data flow

```
avrdude: fuses (BOOTRST, BOOTSZ, BODLEVEL on, DWEN off)
   |
make flash: bootloader + application, built with a PROVISIONING image
   |
agprov --dry-run   -- prints the plan, writes nothing
agprov --commit    -- writes config blocks, locks config zone,
                       generates the DeviceID key, seeds the UDS,
                       locks data zone            (--yes-i-mean-it required)
agprov --verify    -- reads the part back, compares against the template
   |
avrdude: lock bits (BLB0/BLB1/LB mode 3), only now
```

`agprov`'s three modes are described in full in [`cli.md`](cli.md); the exact
`--commit` sequence, matching `runCommit` in `sdk/go/cmd/agprov/steps.go`, is
in [`engineering.md`](engineering.md).

## Trust boundaries

- **Physical access is the assumption the whole model rests on.** There is no
  remote path to a fuse, a lock bit, or a secure-element write. A provisioning
  jig is on a bench with a wired connection, not a fielded device with a radio.
- **The host never holds the UDS.** `ProvSeedUDS` takes a slot and no key
  material — the ATECC608A's own TRNG fills the slot over its internal bus, so
  a compromised provisioning station learns nothing it could use.
- **The device cannot prove its own provisioning state.** Anything a device
  reports about its own fuses or lock bits is something an attacker with code
  execution could also report. The provisioning record — held by the operator,
  outside this repo — is the authority; the device's job is only to prove it
  holds the key that record names.
- **Jig-only, by file selection, not a runtime flag.** `AG_FEATURE_PROVISION`
  defaults to `0`; the Makefile excludes `src/caps/provision.c` from the object
  list and `src/core/commands.def` emits no dispatch entries for `0x70`–`0x75`
  under `#if AG_FEATURE_PROVISION`. A production part answers `AG_ERR_UNKNOWN_CMD`
  to every provisioning command id, because there is no handler to answer with —
  not a disabled one. A device that could be re-provisioned over its own command
  channel would have no identity worth attesting to.

## Alternatives rejected

| Alternative | Rejected because |
|---|---|
| Allow `agprov --commit` to skip the summary CRC on lock | the ATECC608A accepts a no-CRC lock that writes without checking the zone holds what the caller believes; on a part with no erase and no reset that turns one transcription error into permanent scrap. The CRC is mandatory in both the firmware (`ag_prov_cmd_lock`) and the SDK (`Device.ProvLock`). |
| Parse the template as YAML | a YAML library is a dependency, and `sdk/go`'s dependency list is load-bearing — it is imported by go-xkms and go-trusted-platform, so a module added here is added to both. The template format is six keys; a 30-line hand-written parser (`sdk/go/cmd/agprov/template.go`) costs less than a third-party parser in every consumer's graph. |
| Generate the UDS on the host and write it to the slot | a UDS the host generated is a UDS the host has seen, and a provisioning station is exactly the machine an attacker would target to get it. `AG_CMD_PROV_SEED_UDS` takes no key material by design. |
| Let the firmware decide slot policy | a policy encoded in C is not something anyone reads before locking a part. The template is the reviewable artefact; its hash belongs in the provisioning record. |
| A CDI-derived DeviceID by default | ties identity to the exact bootloader/application measurement, so any firmware update forces a new element and a new board. Kept as an opt-in (`CONFIG_DICE_STRICT_DEVICEID`), not the default. |

## What this deliberately does not do

- **No provisioning over the network.** Every write requires a physical serial
  connection to a device running a provisioning image.
- **No host-generated secrets.** The UDS comes from the element's own TRNG,
  never from `agprov` or its operator.
- **No unlock, ever.** The ATECC608A's config and data zones have no erase, no
  reset, and no factory command once locked. See "Decommission" below and
  [`engineering.md`](engineering.md) for the irreversibility guard.
- **No secret readback.** `PROV_PUBKEY` and `ProvPubKey` return a public point
  only; there is no command that returns the contents of a never-readable slot.
- **No enrollment, no registry, no fleet policy.** Provisioning gives a device a
  key. Turning that key into a certificate (ACME `device-attest-01` with an EAB
  credential), enrolling the device, and fleet policy are go-trusted-platform's
  job, not this repo's — see that project's docs. A gap there is filed against
  that repo, never worked around here.

## Decommission

An ATECC608A's locked zones cannot be unlocked — there is no erase, no reset,
no factory command. Decommissioning therefore does not reclaim the part: the
element stays locked and stays with the board it was soldered to. The
procedure is to revoke the DeviceID certificate and delete the registry record
(both in go-trusted-platform) and, where the MCU allows it, chip-erase the
part. This is written down so nobody plans around an unlock that does not
exist.
