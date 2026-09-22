// arduino-generic/sdk/go -- the host side of the AVR wire protocol.
//
// A LAYER 0 ORDINARY LIBRARY, and the dependency list is the whole point.
// go-xkms (Layer 3) and go-trusted-platform (Layer 5) both consume this, so it
// must depend on nothing in the stack: a Layer 0 module that reached upward
// would invert the graph and make itself unimportable by the very projects it
// exists for.
//
// The comparison that makes this concrete: go-xkms/sdk/go resolves to 237
// modules and go-quicbaas/sdk/go to 222, because each replaces in its parent
// and inherits the AWS, Azure, GCP, Pebble and MongoDB closures. Neither is a
// thing a device client can import. go-auth is the model instead -- fourteen
// direct requires and nothing in-house.
//
// One consequence worth stating so it is a decision rather than an accident:
// this package TRANSPORTS attestation evidence and never holds a private key.
// If that ever changes, go-enclave becomes a mandatory dependency -- the
// constitution is absolute that no project hand-rolls locked-memory handling --
// and this module stops being dependency-free.
module github.com/jeremyhahn/arduino-generic/sdk/go

go 1.26.5

require go.bug.st/serial v1.6.4

require (
	github.com/creack/goselect v0.1.2 // indirect
	github.com/stretchr/testify v1.12.1 // indirect
	golang.org/x/sys v0.19.0 // indirect
)
