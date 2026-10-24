# Ethernet transport — CLI

> **Status: DRAFT** · Scope: trusted-firmware · Issues: #55, #75 · Last verified: 2026-10-19
>
> The two `tfwctl` surfaces this feature adds: the `netcfg` verbs, and the TCP
> device address. Read from `sdk/go/cmd/tfwctl` (`netcfg.go`, `tcp.go`,
> `main.go`). The whole `tfwctl` reference, its global flags and every other
> verb, is [`../host-sdk/cli.md`](../host-sdk/cli.md).

See [`user.md`](user.md) for the task walkthrough, [`api.md`](api.md) for the
block's layout.

## Synopsis

```
tfwctl [flags] <serial port>  netcfg set --mac <mac> --ip <addr> --mask <addr> \
                              --gateway <addr> --endpoint <addr:port>
tfwctl [flags] <serial port>  netcfg set --mac <mac> --dhcp --endpoint <addr:port>
tfwctl [flags] <serial port>  netcfg get
tfwctl [flags] tcp://<host>:<port> <command> [args]
```

## `netcfg set` and `netcfg get`

Their flags, output, `--json` keys and refusals are in the `netcfg set` and
`netcfg get` rows of the [tfwctl reference](../host-sdk/cli.md); the
walkthrough, the `DHCP=1` form among it, is [`user.md`](user.md#configure-the-network).

Both verbs go over serial. On the Ethernet link `EEPROM_WRITE` and
`EEPROM_READ` are not on the allow-list, so over a `tcp://` address the device
answers unknown command
([allow-list](adr-wire-protocol-over-w5x00.md#2-allow-list)).

## The TCP device address

A `<port>` of the form `tcp://host:port` is not dialled: the device never
listens. tfwctl listens on `host:port`, which must be the endpoint the device's
network block names, accepts the first device to connect within `-timeout`,
runs the one command on it through `tfw.NewListener(...).Accept()`, and closes
the connection. An empty host (`tcp://:7020`) listens on every address.

| Global flag | Over TCP |
|---|---|
| `-timeout` | bounds both the wait for the device to connect and each reply |
| `-json` | as on serial |
| `-baud`, `-boot-wait` | not used: there is no serial port to open |

Only the allow-listed commands are answered over TCP: `caps`, `app`, `attest`,
`attest-cert`, `attest-csr` and OTA staging (`ota-stage` without `--activate`,
`ota-status`). Every other verb, `auth` and `ota-activate` among them, gets
unknown command; use serial for those ([api.md](api.md)).

```sh
tfwctl -timeout 90s tcp://:7020 caps
```

The device retries on a backoff of 1 s to 64 s
([data flow](architecture.md#data-flow)), so a `-timeout` below that can expire
between two of its attempts.

## Exit codes

| Code | When |
|---|---|
| `0` | the command succeeded |
| `1` | any failure, printed as `tfwctl: <error>` on stderr: a `netcfg` usage error, a flag a parser rejects, `--dhcp` beside `--ip`, `--mask` or `--gateway`, the SDK's `*tfw.NetConfigError`, a device refusal, a `tcp://` address that is not `host:port`, an address that cannot be bound, or `no device connected to <addr> within <timeout>` |
| `2` | fewer than two arguments: the usage text is printed |

Tests: `sdk/go/cmd/tfwctl/netcfg_test.go`, `tcp_test.go`,
`e2e_netcfg_test.go` and `e2e_ethernet_test.go`, all in `make sdk-test`
([engineering.md](engineering.md)).
