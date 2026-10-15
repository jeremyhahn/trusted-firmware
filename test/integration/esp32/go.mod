// The host side of `make esp32-qemu-test`: the Go SDK, over TCP, against the
// firmware running in Espressif's QEMU. A module of its own so sdk/go gains
// no command and no dependency for a test only an ESP-IDF host runs; it takes
// the SDK from this tree, never a published version, so it tests the SDK the
// firmware beside it was built with.
module github.com/jeremyhahn/trusted-firmware/test/integration/esp32

go 1.26.5

require github.com/jeremyhahn/trusted-firmware/sdk/go v0.0.0

replace github.com/jeremyhahn/trusted-firmware/sdk/go => ../../../sdk/go
