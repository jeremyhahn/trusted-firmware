# Ethernet transport — API

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-15

## Wire

Nothing changes on the wire. The frames, command ids, status codes and vectors
are those of [the wire protocol](../wire-protocol/). On Ethernet:

- The device opens the TCP connection; the host accepts it.
- The device sends no events (no BOOT greeting) on Ethernet.
- Only these commands are dispatched. Every other command is answered with
  status `TFW_ERR_UNKNOWN_CMD` (4).

| Id | Command |
|---|---|
| `0x00` | `CAPS` |
| `0x60` | `ATTEST` |
| `0x63` | `ATTEST_CERT` |
| `0x64` | `ATTEST_CSR` |

## Network block

32 bytes at EEPROM address `0x0C0`, written over serial with `EEPROM_WRITE`.
Multi-byte values are big-endian (network order) except where noted.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic, the ASCII bytes `TFNC` |
| 4 | 1 | Version, `1` |
| 5 | 1 | Flags, `0` (reserved) |
| 6 | 6 | MAC address |
| 12 | 4 | Device IPv4 address |
| 16 | 4 | Subnet mask |
| 20 | 4 | Gateway |
| 24 | 4 | Endpoint IPv4 address |
| 28 | 2 | Endpoint TCP port |
| 30 | 2 | CRC-16 of bytes 0–29, the frame CRC, little-endian |

A block with the wrong magic, version or CRC, a zero port or a zero endpoint is
treated as absent: the Ethernet link stays down.

## sdk/go

```go
// Listen opens a TCP listener for devices to connect to.
func Listen(addr string, opts ...Option) (*Listener, error)

// NewListener wraps a listener the caller already owns.
func NewListener(l net.Listener, opts ...Option) *Listener

func (l *Listener) Accept() (*TCPDevice, error)
func (l *Listener) Addr() net.Addr
func (l *Listener) Close() error

// TCPDevice is a Device on an accepted connection.
type TCPDevice struct {
    *Device
    // ...
}
func (d *TCPDevice) RemoteAddr() net.Addr
func (d *TCPDevice) Close() error

// NetConfig is the device's network block.
type NetConfig struct {
    MAC      net.HardwareAddr
    IP       netip.Addr
    Mask     netip.Addr
    Gateway  netip.Addr
    Endpoint netip.AddrPort
}
func (c NetConfig) MarshalBinary() ([]byte, error)

// NetConfigError names the field MarshalBinary refused: a MAC that is not six
// bytes, an address that is not IPv4, or an endpoint the device would treat
// as absent.
type NetConfigError struct {
    Field, Reason string
}

const (
    NetConfigAddr = 0x0C0
    NetConfigLen  = 32
)

// DerivedMAC is the locally administered unicast MAC for a shield whose
// sticker address is not known: the first six bytes of
// SHA-256("tfw-eth-mac-v1" || id), bit 1 of the first octet set and bit 0
// cleared. id is the DeviceID public key (64 bytes, X||Y) on the element tier
// and the 16-byte serial on the software tier.
func DerivedMAC(id []byte) net.HardwareAddr

// WriteNetConfig writes the block over serial, in EEPROM_WRITE chunks.
func (d *Device) WriteNetConfig(ctx context.Context, c NetConfig) error
```

`Option`s given to `Listen` apply to every accepted `Device`. The BOOT wait is
always off for a `TCPDevice`.

## C

`include/tfw/link.h` is the transport's byte link. `include/tfw/eth.h` is the
socket API a controller driver provides. Both are internal to the firmware.
