# Wire protocol — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

## Exported surface

### C — `src/proto/frame.h`

```c
void             tfw_framer_init(tfw_framer_t *f);
void             tfw_framer_abort(tfw_framer_t *f);
tfw_frame_event_t tfw_framer_push(tfw_framer_t *f, uint8_t byte);

uint8_t        tfw_frame_seq(const tfw_framer_t *f);
uint8_t        tfw_frame_op(const tfw_framer_t *f);
tfw_status_t    tfw_frame_status(const tfw_framer_t *f);   /* TFW_OK for a request */
const uint8_t *tfw_frame_payload(const tfw_framer_t *f);
uint8_t        tfw_frame_payload_len(const tfw_framer_t *f);

uint16_t tfw_frame_encode_response(uint8_t *out, uint16_t cap, uint8_t seq,
                                  uint8_t cmd, tfw_status_t status,
                                  const uint8_t *payload, uint8_t plen);
uint16_t tfw_frame_encode_event(uint8_t *out, uint16_t cap, tfw_status_t status,
                               const uint8_t *payload, uint8_t plen);
uint16_t tfw_frame_encode_request(uint8_t *out, uint16_t cap, uint8_t seq,
                                 uint8_t cmd, const uint8_t *payload, uint8_t plen);
```

`tfw_frame_event_t` is `TFW_FRAME_NONE | TFW_FRAME_READY | TFW_FRAME_BAD_CRC |
TFW_FRAME_BAD_FORMAT`. The accessors are valid only immediately after
`TFW_FRAME_READY`.

### C — `src/proto/crc16.h`, `src/proto/transport.h`

```c
uint16_t tfw_crc16_update(uint16_t crc, uint8_t byte);
uint16_t tfw_crc16(const uint8_t *buf, uint16_t len);

void    tfw_transport_init(tfw_transport_t *t);
void    tfw_transport_poll(tfw_transport_t *t);
uint8_t tfw_transport_send_event(tfw_transport_t *t, uint8_t event_id);
```

### C — `src/core/reader.h`, `src/core/writer.h`, `src/core/dispatch.h`

```c
void            tfw_reader_init(tfw_reader_t *r, const uint8_t *buf, uint8_t len);
uint8_t         tfw_read_u8(tfw_reader_t *r);
uint16_t        tfw_read_u16(tfw_reader_t *r);
uint32_t        tfw_read_u32(tfw_reader_t *r);
const uint8_t  *tfw_read_blob(tfw_reader_t *r, uint8_t n);
uint8_t         tfw_reader_remaining(const tfw_reader_t *r);
uint8_t         tfw_reader_ok(const tfw_reader_t *r);

void    tfw_writer_init(tfw_writer_t *w, uint8_t *buf, uint8_t cap);
void    tfw_write_u8/u16/u32(tfw_writer_t *w, ... v);
void    tfw_write_blob(tfw_writer_t *w, const uint8_t *src, uint8_t n);
uint8_t tfw_writer_len(const tfw_writer_t *w);
uint8_t tfw_writer_remaining(const tfw_writer_t *w);
uint8_t tfw_writer_ok(const tfw_writer_t *w);

typedef tfw_status_t (*tfw_cmd_handler_t)(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_dispatch(uint8_t cmd, tfw_reader_t *r, tfw_writer_t *w);
uint8_t     tfw_dispatch_knows(uint8_t cmd);
```

### Go — `sdk/go` (package `tfw`)

```go
func EncodeRequest(seq Seq, cmd Cmd, payload []byte) ([]byte, error)
func EncodeResponse(seq Seq, cmd Cmd, st Status, payload []byte) ([]byte, error)
func Decode(b []byte) (Frame, int, error)
func CRC16(buf []byte) uint16   // crc.go

type Frame struct{ Seq Seq; Op byte; Status Status; Payload []byte }
func (f Frame) IsResponse() bool
func (f Frame) IsEvent() bool
func (f Frame) Cmd() Cmd

type Seq uint8
type Cmd uint8
type Status uint8
func (s Status) String() string
func (s Status) Error() string
func (s Status) AsError() error

type Device struct{ ... }  // device.go
func New(conn io.ReadWriter, opts ...Option) *Device
func (d *Device) Do(ctx context.Context, cmd Cmd, payload []byte) (Frame, error)
func (d *Device) Events() []Frame
```

Every `CmdXxx`/`StatusErrXxx` constant mirrors the C `TFW_CMD_*`/`TFW_ERR_*`
value one for one; the full inventory is `api.md`.

## Invariants

- **LEN is bound-checked on write and on read.** `emit()` in `frame.c` and
  `encode()` in `codec.go` refuse a payload over `TFW_MAX_PAYLOAD` /
  `MaxPayload` before touching the wire; `tfw_framer_push` and `Decode` reject
  any LEN outside `TFW_LEN_MIN..TFW_MAX_LEN` before using it as an index. An
  unchecked length on read folds trailing bytes into the next field —
  `tfw_frame_payload_len` computes `want - 2 - has_status`, which wraps on a
  too-short LEN and hands the dispatcher a length past the buffer.
- **The reader's underflow and the writer's overflow are sticky**, set once
  and never cleared. A handler asks once, at the end, whether its whole
  sequence of reads or writes stayed in bounds; `tfw_dispatch` re-checks both
  after every handler call regardless of what the handler itself checked.
- **Command ids and status codes extend; they never renumber.** See
  `architecture.md`. `TFW_STATUS_MAX` (0xFE) is the ceiling the one-byte
  STATUS field can express.
- **CAPS fields are appended, never reordered.** The Go `Caps()` parser
  (`commands.go`) reads the algorithm bitmap only when the reply is at least
  `capsAlgorithmsEnd` (16) bytes long, so firmware built before that field
  existed still parses correctly and reports the empty algorithm set rather
  than an error.
- **A SEQ of `TFW_SEQ_NONE` (0) means no correlation is wanted**; the device
  still echoes it, so a reply carrying SEQ 0 is not evidence of which
  request it answered. `Device.nextSeq` in `device.go` never allocates 0 for
  an actual request.
- **Events emit only when no response is in flight.**
  `tfw_transport_send_event` returns 0 and queues nothing while `t->out_len`
  is nonzero; the caller retries on a later `tfw_transport_poll`. This keeps
  the device a strict request/response state machine.
- **The golden vectors are literals, asserted equal across languages by
  `make verify-vectors`.** A failure there is a wire break, not a test to
  update.
- **No wait on a peripheral register is unbounded.** `hal_bus.c`'s I2C and SPI
  paths both poll with a spin limit (`TWI_SPIN_LIMIT`, `SPI_SPIN_LIMIT`) and
  return `TFW_ERR_TIMEOUT` rather than loop forever; `PULSE_IN` bounds the same
  way at the capability layer. A bus held by a stuck peripheral -- a shorted
  clock, a slave holding the line -- costs one refused command, not the
  device's ability to answer any other (issue #16).
- **Every peripheral the HAL exports an init for is called exactly once at
  boot**, from `main.c`: `tfw_hal_adc_init()` unconditionally, `tfw_bus_init()`
  (`src/caps/bus.c`) for I2C and SPI. Neither is gated by an `#if` in
  `main.c` -- `tfw_bus_init()` or `bus_none.c`'s no-op stub is always the one
  linked, chosen by `mk/firmware.mk`'s `BUS_SRCS` the same way `CRYPTO_SRCS`
  chooses a crypto tier's `tfw_crypto_init()`. Before this, `ADEN` and `SPE`
  were never set: an ADC read returned a plausible-looking zero and the
  first `SPI_XFER` hung the device forever (issue #19).

## Error semantics

One status vocabulary, `tfw_status_t` / `Status`, carried in the wire's
STATUS byte — never a second in-band sentinel. Full table in `api.md`.
Categories, by what raises them:

| layer | statuses |
|---|---|
| framing/transport (below the dispatcher) | `TFW_ERR_CRC`, `TFW_ERR_FRAMING`, `TFW_ERR_TOO_LONG` |
| dispatch (request never reached a capability) | `TFW_ERR_UNKNOWN_CMD`, `TFW_ERR_BAD_ARGC` |
| capability argument validation | `TFW_ERR_BAD_PIN`, `TFW_ERR_UNSUPPORTED`, `TFW_ERR_BUSY`, `TFW_ERR_TIMEOUT`, `TFW_ERR_NACK`, `TFW_ERR_RANGE`, `TFW_ERR_NO_SLOT`, `TFW_ERR_RESERVED_PIN` |
| trust | `TFW_ERR_CRYPTO`, `TFW_ERR_NOT_PROVISIONED`, `TFW_ERR_PROTECTED_REGION`, `TFW_ERR_AUTH_REQUIRED` |
| firmware bug | `TFW_ERR_INTERNAL` |

On the Go side, errors are types grouped by what a caller does next
(resynchronise, retransmit, chunk, reconnect, ...); the full table is in
`../host-sdk/architecture.md`. `Status` itself implements `error`, and
`Frame.Status.AsError()` returns `nil` for `StatusOK`.

## Configuration

All in `include/tfw/config.h`, resolved `-D` > board profile > default:

| knob | default | effect |
|---|---|---|
| `TFW_MAX_PAYLOAD` | 64 | largest payload accepted/emitted; sizes both frame buffers |
| `TFW_MAX_LEN` (derived) | `TFW_MAX_PAYLOAD + 3` | must fit one byte; `_Static_assert`ed |
| `TFW_MAX_FRAME` (derived) | `TFW_MAX_LEN + 4` | largest frame on the wire |
| `TFW_FRAME_IDLE_MS` | 250 | how long a partial frame may sit before the framer abandons it |
| `TFW_UART_BAUD` | 115200 | matched by `serialport.DefaultBaud` in the Go SDK |
| `TFW_PROTO_VERSION_MAJOR` / `_MINOR` | 1 / 0 | rejected on major mismatch, tolerated in both directions on minor |
| `TFW_I2C_DEFAULT_HZ` | 100000 | `tfw_bus_init()`'s boot-time I2C clock |
| `TFW_I2C_HZ_MIN` / `_MAX` | 1000 / 400000 | `I2C_CONFIG`'s accepted interval, capped at Fast-mode |
| `TFW_SPI_DEFAULT_MODE` / `_CLKDIV` / `_LSB_FIRST` | 0 / 3 / 0 | `tfw_bus_init()`'s boot-time SPI mode 0, slowest divider, MSB-first |

## Test strategy

| what | how | run |
|---|---|---|
| framer correctness | `test/unit/test_frame.c`, Unity | `make test` |
| framer robustness | ten million random bytes under AddressSanitizer, asserting no overrun/allocation/hang | `make test-asan` |
| Go codec robustness | `FuzzDecode`, `FuzzReplyParsers` in `sdk/go/fuzz_test.go` | `go test -fuzz=FuzzDecode -fuzztime=5m` |
| cross-implementation agreement | Go SDK talks to `tfwsim` (real firmware, compiled for the host) over a pipe | `sdk-test`, driven by `make tfwsim` |
| wire pinning | `testdata/vectors.h` vs `sdk/go/internal/vectors/vectors.go` -- CRC results and, since #21, full encoded request frames for `I2C_CONFIG`/`SPI_CONFIG` | `make verify-vectors` |
| ADC and SPI against the emulated peripherals | `test/integration/simavr/harness.c` feeds `ADC_IRQ_OUT_TRIGGER`/`SPI_IRQ_OUTPUT` and bounds every exchange by cycle count, so a hang fails rather than blocks | `make integration-test` |
| CRC correctness | published CRC-16/CCITT-FALSE check value plus order-sensitivity vectors | `sdk/go/crc_test.go`, `test/unit/test_frame.c` |
| codec unit tests | request/response/event round trip, short-frame, bad-CRC, bad-format, resync | `sdk/go/codec_test.go` |

## Observability

`tfw_framer_t` carries `err_crc` and `err_format`, saturating counters bumped
on every `TFW_FRAME_BAD_CRC` / `TFW_FRAME_BAD_FORMAT`. `TFW_CMD_SYS_INFO`'s
handler (`tfw_sys_cmd_sys_info` in `src/caps/sys.c`) reports both, appended
after `reset_cause` (#5) rather than inserted, so a host built before they
existed reads the same seven bytes it always has and a newer host reads four
more. `main.c` and `tools/tfwsim/main.c` are what makes this possible without
`tfw_sys_cmd_sys_info` reaching the framer directly: `src/core/dispatch.h`
forbids a command handler from touching the framer or the transport, so the
superloop -- the one place that owns both -- mirrors the counters into
`src/caps/sys.c` once a pass, through `tfw_sys_set_framer_errors()`.

Beyond that, a host observes the protocol only through what a request
returns: a `Status` on every response, `Frame.IsEvent()` for unsolicited
frames, and the typed errors in `sdk/go/errors.go` for anything that never
became a well-formed frame at all.
