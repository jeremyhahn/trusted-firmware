// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package xkms

// Registration of the AVR backend with go-xkms goes here.
//
// NOT IMPLEMENTED YET, ON PURPOSE. go-xkms#1439 step 1 adds the backend
// registry hook this module registers against, and it is not released: the
// module requires go-xkms v0.51.0-alpha, which predates it. Writing a stub
// against an interface that does not exist would guess at its shape, and the
// guess would have to be undone. When #1439 ships, bump the go-xkms requirement
// and register avr.Backend's constructor here, in an init or an exported
// Register function as the hook prescribes.
