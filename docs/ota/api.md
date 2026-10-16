# OTA — API

> **Status: DRAFT** · Issue: #75, #52 · Scope: trusted-firmware · Last verified: 2026-10-16
> What is on the wire and in `sdk/go` on main. The frames, the other commands
> and the vectors are those of [the wire protocol](../wire-protocol/).

## Wire

An application image built with the stager (`OTA_STAGE=1`, see
[engineering.md](engineering.md)) answers these four commands. An image without
it answers `TFW_ERR_UNKNOWN_CMD` (4). All four are on the Ethernet allow-list
and answer on serial and TCP alike. Multi-byte values are little-endian.

| Id | Command | Request | Reply |
|---|---|---|---|
| `0x68` | `OTA_BEGIN` | `[total_len:4]` | none |
| `0x69` | `OTA_DATA` | `[offset:4][bytes:1..]`, up to `TFW_MAX_PAYLOAD` in all | none |
| `0x6A` | `OTA_END` | none | none |
| `0x6B` | `OTA_STATUS` | none | `[state:1][received:4]` |

`OTA_STATUS`'s `state`:

| Value | State |
|---|---|
| 0 | idle |
| 1 | receiving |
| 2 | complete: the image is checked and `MARK` is written |
| 3 | failed |

Status codes (`include/tfw/status.h`) the stager returns:

| Command | Status | When |
|---|---|---|
| `OTA_BEGIN` | `TFW_ERR_RANGE` (11) | `total_len` is not above the trailer's length, is above the store's `STAGE` area, or is not a whole number of `TFW_OTA_PAGE_MAX` (256) pages |
| `OTA_DATA` | `TFW_ERR_BUSY` (8) | no transfer is receiving |
| `OTA_DATA` | `TFW_ERR_RANGE` (11) | `offset` is not the received count, or the bytes run past `total_len` |
| `OTA_END` | `TFW_ERR_BUSY` (8) | no transfer is receiving |
| `OTA_END` | `TFW_ERR_FRAMING` (2) | fewer than `total_len` bytes arrived |
| `OTA_END` | `TFW_ERR_UNSUPPORTED` (7) | the trailer's magic or version is wrong, or its board id is not this board's |
| `OTA_END` | `TFW_ERR_RANGE` (11) | the trailer's image length is zero or reaches the trailer |
| `OTA_END` | `TFW_ERR_CRYPTO` (14) | the SHA-256 of the image is not the trailer's measurement |
| any | `TFW_ERR_BAD_ARGC` (5) | the payload length is wrong for the command |

A store failure during `OTA_BEGIN`, `OTA_DATA` or `OTA_END` returns the store
driver's status. Every failure after `OTA_BEGIN` has been accepted leaves the
state `failed`; a new `OTA_BEGIN` starts over. `OTA_BEGIN` erases `MARK` first,
so a staged image is never half-replaced.

The stager does not check the signature or the floor. tfw-boot does, at the
next reset ([architecture.md](architecture.md)).

### Not on the wire

`OTA_CONFIRM`, `OTA_ACTIVATE` and `OTA_INFO` are the ADR's decision 8 and
decision 13 proposal. They have no command id in `include/tfw/protocol.h` and
no row in `src/core/commands.def`, and an image answers them
`TFW_ERR_UNKNOWN_CMD`. `TFW_ERR_BAD_STATE` (19) is defined for them and the
stager does not return it.

## `sdk/go`

`sdk/go/ota.go`, on `*tfw.Device` (and on `*tfw.TCPDevice`, which embeds it):

| Name | Does |
|---|---|
| `OTABegin(ctx, total uint32) error` | `OTA_BEGIN` |
| `OTAData(ctx, offset uint32, chunk []byte) error` | one `OTA_DATA` |
| `OTAEnd(ctx) error` | `OTA_END` |
| `OTAStatus(ctx) (OTAState, uint32, error)` | `OTA_STATUS`: the state and the received count |
| `StageImage(ctx, image []byte) error` | `OTA_BEGIN`, then `OTA_DATA` in chunks of `OTAChunk` (60) bytes, then `OTA_END` |

`OTAState` has `OTAIdle`, `OTAReceiving`, `OTAComplete` and `OTAFailed`, the
values above. `StageImage` sends the image as given: it neither pads it nor
checks its trailer. A device status comes back as the SDK's status error.

The same calls drive tfw-boot's own serial session, which takes the same four
commands.
