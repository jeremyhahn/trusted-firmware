### Added

- **`tfwctl unix:<path>`** (#161). tfwctl dials an emulator's UART on a Unix
  socket, as QEMU serves it under `-serial unix:<path>,server=on`, and talks to
  it as to a serial port (`sdk/go/cmd/tfwctl/unix.go`).
