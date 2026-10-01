// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package aggeneric

import (
	"context"
	"crypto"
	"crypto/rand"
	"crypto/sha256"
	"encoding/asn1"
	"errors"
	"math/big"
)

// errASN1TrailingData and errASN1BadSig are the two ways a decoded ASN.1
// ECDSA signature can be well-formed DER and still not be one: extra bytes
// after the SEQUENCE, or an R/S this curve cannot have produced.
var (
	errASN1TrailingData = errors.New("aggeneric: auth: trailing data after ASN.1 signature")
	errASN1BadSig       = errors.New("aggeneric: auth: signature is not a valid P-256 (r, s) pair")
)

// The operator-signature auth session -- issue #4. ELEMENT TIER ONLY: a
// soft-tier or CRYPTO=none device does not compile in AUTH_CHALLENGE/
// AUTH_RESPONSE at all and answers StatusErrUnknownCmd, exactly like any other
// absent feature. See docs/security/threat-model.md for why that gap is
// accepted rather than worked around.
//
// This package never holds or generates the operator private key -- it is
// Layer 0 and holds no key material (see CLAUDE.md's SDK boundary). AuthResponse
// takes a crypto.Signer, the standard library's own custody-agnostic
// interface, so key custody is entirely the caller's business: an in-memory
// *ecdsa.PrivateKey for a bench, or a signer backed by an HSM or
// go-trusted-platform's own key store in a fleet.
const (
	CmdAuthChallenge Cmd = 0x61
	CmdAuthResponse  Cmd = 0x62
)

// AuthNonceLen is the challenge's width.
const AuthNonceLen = 16

// authDomain domain-separates the signed message from anything else an
// operator key might ever be asked to sign, and from the message ATTEST's
// evidence or ATTEST_CERT's enrollment challenge would produce -- three
// different questions, three different byte strings, on purpose.
const authDomain = "ag-auth-v1"

// AuthChallenge asks the device for a fresh, single-use nonce.
//
// The device clears any earlier established session the moment it issues a
// new challenge: asking for one is the host declaring it wants to re-prove
// itself, and a stale grant must not survive that untouched. The returned
// nonce is consumed by the first AuthResponse the device receives, whether or
// not that response's signature verifies -- a captured attempt cannot be
// replayed against it.
func (d *Device) AuthChallenge(ctx context.Context) ([]byte, error) {
	f, err := d.Do(ctx, CmdAuthChallenge, nil)
	if err != nil {
		return nil, err
	}
	if len(f.Payload) != AuthNonceLen {
		return nil, &ShortReplyError{Cmd: CmdAuthChallenge, Got: len(f.Payload), Need: AuthNonceLen}
	}
	return f.Payload, nil
}

// AuthResponse signs the challenge with the operator key behind signer and
// sends the signature back, establishing an authenticated session that lasts
// until the device resets -- there is no
// separate inactivity timeout.
//
// serial is the device's own 16-byte platform identity -- SerialLen bytes,
// the same value Attest's evidence carries as DeviceID. The signed message is
// domain||serial||nonce so a signature captured from one device can never be
// replayed against another: binding it here, on the host side, is what lets
// the device do the same binding on its side with nothing more than the
// nonce it already holds and the serial it was provisioned with.
//
// signer must produce a P-256 ECDSA signature ASN.1-encoded as SEQUENCE{r,
// s} -- the universal convention for crypto.Signer over an *ecdsa.PrivateKey,
// and the one every ECDSA-capable HSM/PKCS#11 signer this package has been
// tested against also follows.
func (d *Device) AuthResponse(ctx context.Context, signer crypto.Signer, nonce, serial []byte) error {
	if len(nonce) != AuthNonceLen {
		return &ArgRangeError{
			Cmd: CmdAuthResponse, Arg: "nonce", Value: int64(len(nonce)),
			Min: AuthNonceLen, Max: AuthNonceLen, Unit: "bytes",
		}
	}
	if len(serial) != SerialLen {
		return &ArgRangeError{
			Cmd: CmdAuthResponse, Arg: "serial", Value: int64(len(serial)),
			Min: SerialLen, Max: SerialLen, Unit: "bytes",
		}
	}

	msg := make([]byte, 0, len(authDomain)+SerialLen+AuthNonceLen)
	msg = append(msg, authDomain...)
	msg = append(msg, serial...)
	msg = append(msg, nonce...)
	digest := sha256.Sum256(msg)

	der, err := signer.Sign(rand.Reader, digest[:], crypto.SHA256)
	if err != nil {
		return &AuthSignError{Op: "sign", Err: err}
	}
	sig, err := ecdsaDERToRaw(der)
	if err != nil {
		return &AuthSignError{Op: "decode signature", Err: err}
	}

	_, err = d.Do(ctx, CmdAuthResponse, sig)
	return err
}

// AuthSignError reports that the caller-supplied signer failed, or produced
// something this package could not turn into the wire's raw R||S form. Never
// a key-custody failure this package could have prevented -- it holds no key
// to have gotten right.
type AuthSignError struct {
	Op  string
	Err error
}

func (e *AuthSignError) Error() string {
	return "aggeneric: auth: " + e.Op + ": " + e.Err.Error()
}

func (e *AuthSignError) Unwrap() error { return e.Err }

var _ error = (*AuthSignError)(nil)

// ecdsaRawSigLen is AG_SIG_LEN: a P-256 signature as raw R||S, 32 bytes each,
// the same width every signature this wire protocol carries uses (ATTEST's,
// ATTEST_CERT's certificate signature, and this one).
const ecdsaRawSigLen = 64

type ecdsaASN1Sig struct {
	R, S *big.Int
}

// ecdsaDERToRaw unpacks the ASN.1 SEQUENCE{r, s} crypto.Signer's ECDSA
// convention produces into the fixed-width big-endian pair the wire expects.
func ecdsaDERToRaw(der []byte) ([]byte, error) {
	var sig ecdsaASN1Sig
	rest, err := asn1.Unmarshal(der, &sig)
	if err != nil {
		return nil, err
	}
	if len(rest) != 0 {
		return nil, errASN1TrailingData
	}
	if sig.R == nil || sig.S == nil || sig.R.Sign() < 0 || sig.S.Sign() < 0 {
		return nil, errASN1BadSig
	}
	if sig.R.BitLen() > 256 || sig.S.BitLen() > 256 {
		return nil, errASN1BadSig
	}
	out := make([]byte, ecdsaRawSigLen)
	sig.R.FillBytes(out[:32])
	sig.S.FillBytes(out[32:])
	return out, nil
}
