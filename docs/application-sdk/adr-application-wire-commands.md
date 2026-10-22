# ADR: application-defined wire commands

> **Status: PROPOSED** · Row: `application-sdk` · Scope: trusted-firmware
> (device and `sdk/go`) · Last verified: 2026-10-20, against
> `include/tfw/protocol.h`, `src/core/commands.def`, `src/core/dispatch.c`,
> `src/proto/transport.c`, `src/proto/link_eth.c` and `src/core/app_runner.c`
>
> Nothing in the code changes until the owner rules. Every decision below is a
> recommendation with its alternatives. The open questions are listed at the
> end.

## The question

The `application-sdk` row names application-defined wire commands as a
proposed follow-up ([architecture.md](architecture.md#what-it-does-not-do)).
Today an application can read its configuration region and report its 32
bytes of evidence, but a host cannot ask it anything. The first application,
the secure BMC, needs that: power control and sensor reads are its own
operations, not the firmware's.

This ADR decides how an application's commands reach the wire: which ids they
take, how the firmware hands a request to the application without a vtable,
what the dispatcher still checks, when the application is asked, who may send
the commands on each link and tier, and what `sdk/go` offers a host.

No standard covers this: the wire protocol is this project's own
(`docs/wire-protocol/`). The constraints come from CLAUDE.md: command ids
extend and never renumber, the HAL and the hooks link at link time, and
preprocessor conditionals stay in the four places.

## Context

- The OP byte of a request is 7 bits, `0x00..0x7F`. A response carries
  `CMD | 0x80`, and `0xFF` is the event marker (`include/tfw/protocol.h:10-12`).
  So `0x7F` can never be a command: its response would read as an event.
  Today the device answers a `0x7F` request with `TFW_ERR_UNKNOWN_CMD` in a
  frame whose OP is `0xFF` (`src/proto/transport.c:72`), which a host reads as
  an event carrying the request's SEQ.
- The firmware groups its ids by high nibble, and all eight groups are taken:
  system `0x0_`, GPIO `0x1_`, analog `0x2_`, pulse `0x3_`, EEPROM `0x4_`,
  buses `0x5_`, attestation and OTA `0x6_`, provisioning `0x7_`. The free ids
  are the gaps inside the groups, 76 in all. The largest run is
  `0x42..0x4F`, 14 ids.
- `src/core/dispatch.c` checks each command's payload bounds before its
  handler runs, and after it the writer's and the reader's sticky overflow, so
  `TFW_ERR_BAD_ARGC` and `TFW_ERR_TOO_LONG` come from one place.
- `APP_INFO` (`0x06`) is present only in an image built with an `APP_DIR`
  (`TFW_FEATURE_APP`, `src/core/commands.def:33`). It is on the Ethernet
  allow-list (`src/proto/link_eth.c:127`), read-only and unauthenticated.
- The hooks are strong symbols the application defines. Weak defaults and a
  table of function pointers are both rejected
  ([architecture.md](architecture.md#rejected-alternatives)).
- On the element tier `tfw_auth_gate()` guards `RESET` mode 1 and the reserved
  EEPROM reads. Every other tier links the permissive gate. The application
  may include only `include/tfw/*.h` and has no interface to the auth session.

## Decisions proposed

### 1. One firmware id, `APP_CMD` `0x07`, with the application's own sub-op

`APP_CMD` takes `0x07`, beside `APP_INFO` in the system group. Its request is
`[app_op:1][args:0..63]`, and its reply is whatever the application writes.
The application owns the 256 values of `app_op` and versions them with its
descriptor's version. They extend and never renumber, the same rule the
firmware keeps for its own ids, and `docs/` says so for the application's
author.

| Option | Ids spent | Commands an application gets | Cost |
|---|---|---|---|
| **A. One id, a sub-op byte (recommended)** | 1 | 256 | one byte of each request |
| B. A block of ids, `0x78..0x7E` | 7 | 7 | the provisioning group's last free ids; an application with an eighth command has nowhere to go |
| C. A second OP namespace (an escape OP, or a wider OP) | 1 or 0 | 256 or more | a frame format change, every host and every vector |

A keeps the firmware's namespace almost whole. B lets a future firmware
command and an application command compete for the same ids. C breaks the
wire.

### 2. The seam: one strong hook, picked by the object list

```c
tfw_status_t tfw_app_command(uint8_t op, tfw_reader_t *r, tfw_writer_t *w);
```

It is declared in `include/tfw/app.h`. `APP_CMD`'s handler,
`tfw_app_cmd_command()`, sits in `src/core/app_runner.c` beside
`tfw_app_cmd_info()`. It reads `op`, checks the runner's state (decision 4)
and calls the hook.

An application that has no commands sets nothing. `mk/app.mk` then links
`src/core/app_cmd_none.c`, which answers every `op` as in decision 3. An
application that has commands sets `APP_COMMANDS := yes` in its `app.mk` and
defines the hook. Defining it without setting the knob is a duplicate symbol,
and setting the knob without defining it is a missing one. Both fail at link
time, as the other hooks do.

`APP_CMD` is present whenever `APP_INFO` is: in every image built with an
`APP_DIR`, and in none other. Its `commands.def` entry sits in the same
`#if TFW_FEATURE_APP` block. So a host that sees `APP_INFO` answer knows
`APP_CMD` is there. The descriptor does not change and `TFW_APP_ABI` stays,
because no field is added and an application without commands links as it
does today.

### 3. What the dispatcher checks, and the three refusals

The dispatcher's bounds for `APP_CMD` are `1..TFW_MAX_PAYLOAD`. A request with
no `app_op` is `TFW_ERR_BAD_ARGC`, from the one place it always comes from.
The post-checks apply unchanged, because the hook runs inside `APP_CMD`'s
handler: a reply that overflows is `TFW_ERR_TOO_LONG`, and a read past the
arguments is `TFW_ERR_BAD_ARGC`. Each sub-op's own arity is the application's
to check. `docs/application-sdk/api.md` will say that a sub-op with fixed
arguments checks that `tfw_reader_remaining()` is 0 when it has read them.

A host can tell three cases apart, with no new status code:

| Answer | Means |
|---|---|
| `TFW_ERR_UNKNOWN_CMD` | no application is linked (the image has no `APP_CMD`) |
| `TFW_ERR_BAD_STATE` | an application is linked and is not running (decision 4) |
| `TFW_ERR_UNSUPPORTED` | the application runs, and has no such `app_op` |

The alternative, `TFW_ERR_UNKNOWN_CMD` for an unknown `app_op` too, loses the
first distinction. A host would need `APP_INFO` to learn whether the image or
the application lacks the command.

`TFW_ERR_BAD_STATE` (19) is described today as the refusal of a command in the
wrong OTA state (`include/tfw/status.h:53`). This widens its comment to the
wrong state of the OTA session or of the application. Its number and its
existing meaning stay. The alternative is a new code, 20, for the application's
state alone (open question 5).

### 4. Asked only while the application runs, within its budget

The hook is called only in `RUNNING`. In `IDLE`, `FAILED`, `UNSUPPORTED` and
`STOPPED` the handler answers `TFW_ERR_BAD_STATE` and calls nothing. That keeps
the runner's rule that no hook is called in a state it did not put the
application in. A call takes the same budget as `run`
(`TFW_APP_RUN_BUDGET_MS`), and a call over it counts one overrun in the counter
`APP_INFO` reports. The reply is sent when the hook returns, as for every
command. An operation that takes longer starts in the hook and finishes in
`run`, and the host polls with a second sub-op.

### 5. Authorization: the application decides per sub-op

An application command can actuate, for example a BMC powering its host off.
The capability commands (`DIGITAL_WRITE` and the rest) are not gated on any
tier, so an ungated `APP_CMD` would be no weaker than the pins it drives. But an
application knows which of its operations matter, and the firmware does not.

| Option | Element tier | Other tiers |
|---|---|---|
| a. Never gated | any host on the link | the same |
| **b. The application asks, per sub-op (recommended)** | `tfw_app_session_ok()` in `app.h` answers whether an authenticated session is open; the hook refuses with `TFW_ERR_AUTH_REQUIRED` | permissive, as `tfw_auth_gate()` is today |
| c. Always gated | every `APP_CMD` needs the session | permissive |

b is a read-only view of the session. It gives the application no key and no
way to open a session, so "secrets never pass through the application" still
holds ([architecture.md](architecture.md#trust-boundaries)). c would make
read-only sub-ops, such as a sensor read, cost a challenge round.

### 6. Serial only, until the owner widens the Ethernet allow-list

`APP_CMD` is not added to `tfw_link_admits()`, so over Ethernet it answers
`TFW_ERR_UNKNOWN_CMD`, as every command off the list does. The allow-list is
a decision about what the network may ask for (`src/proto/link_eth.c:125`).
The OTA ADR took the same decision for `OTA_INFO`, `OTA_ACTIVATE` and
`OTA_CONFIRM`. The BMC on the ESP32-P4 speaks only Ethernet, so for it this
leaves the commands unreachable until the owner rules (open question 2).

### 7. Host side and vectors

`sdk/go` gains `CmdAppCmd = 0x07` and
`(*Device).AppCommand(ctx, op uint8, args []byte) ([]byte, error)`. It returns
the reply bytes undecoded, because their shape is the application's. It
refuses more than 63 bytes of `args` with an `*ArgRangeError` before
anything is sent. `tfwctl` gains `app-cmd <op> [hex-args]`. The example
application in `test/app/example/` gains one sub-op, and the literal vectors
`TFWV_APPCMD_*` in `testdata/vectors.h`, mirrored in `sdk/go/internal/vectors`,
pin its request and reply, the `BAD_STATE` and `UNSUPPORTED` refusals and the
`UNKNOWN_CMD` of an image with no application. No existing vector changes.

## Consequences

- The firmware spends one id, `0x07`, and adds one `commands.def` entry, one
  handler, one `.c` file for applications with no commands and one `app.mk`
  knob. RAM: none. Flash: the handler and the stand-in, in images with an
  application only.
- An application's sub-ops are its own wire contract. A verifier binds them to
  the code through `FWHash`, as it does the rest of the application.
- `0x7F` is marked never assignable in `include/tfw/protocol.h`, and
  `test_no_command_id_reaches_the_event_marker` (`test/unit/test_dispatch.c`)
  pins that no table entry reaches it. Neither changes the wire. The
  device's answer to a `0x7F` request, a frame with OP `0xFF`, is a separate
  question (open question 4), because changing it changes the wire.

## Open questions for the owner

1. Decision 1: one id with a sub-op byte (A), a block of ids (B), or a wider
   OP (C)? Recommended: A, at `0x07`.
2. Decision 6: is `APP_CMD` admitted over Ethernet? If it is, it is admitted
   for every application, or per application by a second `app.mk` knob that
   links one of two `.c` files. The BMC needs it on the network.
3. Decision 5: does the application decide per sub-op (b), or is every
   `APP_CMD` gated on the element tier (c)? Recommended: b.
4. Should the device answer a `0x7F` request at all? Today it answers with OP
   `0xFF`, which reads as an event. Ignoring it, as it ignores a request with
   the response bit set, would be a wire change with a vector.
5. Decision 3: does a stopped or failed application answer
   `TFW_ERR_BAD_STATE` (19, widened), or a new status code (20)? Recommended:
   19.

The pin reservation, the row's other proposed follow-up, is a separate
decision and is not taken here.
