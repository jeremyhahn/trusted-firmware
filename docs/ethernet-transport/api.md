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
| `0x06` | `APP_INFO` |
| `0x60` | `ATTEST` |
| `0x63` | `ATTEST_CERT` |
| `0x64` | `ATTEST_CSR` |
| `0x68` | `OTA_BEGIN` |
| `0x69` | `OTA_DATA` |
| `0x6A` | `OTA_END` |
| `0x6B` | `OTA_STATUS` |

The list is `tfw_link_admits` in `src/proto/link_eth.c`. An id on it that the
image does not carry still answers `TFW_ERR_UNKNOWN_CMD` (4), as it does on
serial: `APP_INFO` needs an application, and the four `OTA_*` commands need the
stager ([OTA API](../ota/api.md)).

## Network block

32 bytes at EEPROM address `0x0C0`, written over serial with `EEPROM_WRITE`.
Multi-byte values are big-endian (network order) except where noted.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic, the ASCII bytes `TFNC` |
| 4 | 1 | Version, `1` |
| 5 | 1 | Flags. Bit 0 (`TFW_NETCFG_DHCP`): take the IP, mask and gateway from a DHCP lease instead of bytes 12–23; an image built without `DHCP=1` keeps the link down. Bits 1–7 reserved, `0`. `sdk/go`'s `NetConfig.DHCP` is bit 0; it writes the reserved bits `0` and ignores them on read |
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
    DHCP     bool             // flags bit 0: lease the IP, mask and gateway
    MAC      net.HardwareAddr
    IP       netip.Addr
    Mask     netip.Addr
    Gateway  netip.Addr
    Endpoint netip.AddrPort
}
func (c NetConfig) MarshalBinary() ([]byte, error)

// UnmarshalBinary decodes a block, refusing what tfw_netcfg_decode treats as
// absent: no TFNC magic, another version, a CRC mismatch, a zero endpoint
// address or port.
func (c *NetConfig) UnmarshalBinary(b []byte) error

// NetConfigError names the field MarshalBinary or UnmarshalBinary refused: a
// MAC that is not six bytes, an address that is not IPv4, an endpoint the
// device would treat as absent, or ("block") a block that does not decode.
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

// ReadNetConfig reads the block back over serial, in EEPROM_READ chunks, and
// decodes it. The block is above the element tier's gated reads.
func (d *Device) ReadNetConfig(ctx context.Context) (NetConfig, error)
```

`tfwctl netcfg set` and `tfwctl netcfg get` are these two calls from the
command line ([tfwctl reference](../host-sdk/cli.md)). `tfwprov --bake-netblock`
writes the same block into an `.eep` file
([tfwprov reference](../device-provisioning/cli.md)).

Both take `--dhcp` in place of `--ip`, `--mask` and `--gateway`. It sets
`NetConfig.DHCP` (flags bit 0) and writes bytes 12–23 as zero. Either tool
refuses `--dhcp` with any of `--ip`, `--mask` or `--gateway` beside it, and
writes nothing; `--mac` and `--endpoint` are still required. `netcfg set` and
`netcfg get` print the flag as `dhcp`; with `--json` it is the boolean key
`dhcp`.

`Option`s given to `Listen` apply to every accepted `Device`. The BOOT wait is
always off for a `TCPDevice`.

## C

`include/tfw/link.h` is the transport's byte link. `include/tfw/eth.h` is the
socket API a controller driver provides. Both are internal to the firmware.
