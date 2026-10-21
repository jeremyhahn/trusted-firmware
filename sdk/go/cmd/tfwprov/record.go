// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The provisioning record --record writes: the device's UEID joined to its
// DeviceID public key, for go-trusted-platform's device registry to ingest
// instead of hand-copied stdout (follow-up 6 of
// docs/dice-attestation/adr-possession-at-acme-finalize.md).
//
// No member is ours. The record is a JSON claims set of two registered
// claims:
//
//	ueid  RFC 9711 §4.2.1, the UEID: type byte 0x01 (RAND) followed by the
//	      16-byte serial --bake-eeprom wrote, which is what the device
//	      claims in its own evidence. A bstr, so base64url without padding
//	      in JSON (RFC 9711 §7.2.2).
//	cnf   RFC 7800 §3.2, the confirmation claim's "jwk" member: the DeviceID
//	      public key as an RFC 7517 JWK, EC P-256 per RFC 7518 §6.2.1.
//
// The SDK computes no RFC 7638 thumbprint, so the record carries none; the
// JWK's required members are exactly what one would hash.

// ueidTypeRAND is RFC 9711 §4.2.1's UEID type byte for a random number.
const ueidTypeRAND = 0x01

type provRecord struct {
	UEID string        `json:"ueid"`
	Cnf  *confirmation `json:"cnf,omitempty"`
}

type confirmation struct {
	JWK *ecJWK `json:"jwk"`
}

type ecJWK struct {
	Kty string `json:"kty"`
	Crv string `json:"crv"`
	X   string `json:"x"`
	Y   string `json:"y"`
}

// recordError is a refusal: the record and what this run knows do not belong
// to one device, or the file is not a record. Nothing is written.
type recordError struct{ msg string }

func (e *recordError) Error() string { return "--record: " + e.msg }

func ueidOf(serial [tfw.SerialLen]byte) string {
	return base64.RawURLEncoding.EncodeToString(append([]byte{ueidTypeRAND}, serial[:]...))
}

// jwkOf encodes the DeviceID point PROV_GENKEY and PROV_PUBKEY return, X||Y.
func jwkOf(xy []byte) (*ecJWK, error) {
	if err := requireOnCurveP256(xy); err != nil {
		return nil, &recordError{"the DeviceID point is " + err.Error()}
	}
	half := len(xy) / 2
	return &ecJWK{
		Kty: "EC",
		Crv: "P-256",
		X:   base64.RawURLEncoding.EncodeToString(xy[:half]),
		Y:   base64.RawURLEncoding.EncodeToString(xy[half:]),
	}, nil
}

// readRecord reads a record and refuses anything that is not one: an unknown
// member would be dropped by the rewrite, and a UEID of another shape is not
// a device this tool baked.
func readRecord(path string) (*provRecord, error) {
	raw, err := os.ReadFile(path) // #nosec G304 -- the operator names the record
	if err != nil {
		return nil, fmt.Errorf("--record: %w", err)
	}
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.DisallowUnknownFields()
	var r provRecord
	if err := dec.Decode(&r); err != nil {
		return nil, &recordError{fmt.Sprintf("%s is not a provisioning record: %v", path, err)}
	}
	id, err := base64.RawURLEncoding.DecodeString(r.UEID)
	if err != nil || len(id) != 1+tfw.SerialLen || id[0] != ueidTypeRAND {
		return nil, &recordError{fmt.Sprintf("%s: ueid is not 0x01 followed by a %d-byte serial", path, tfw.SerialLen)}
	}
	if r.Cnf != nil {
		if r.Cnf.JWK == nil {
			return nil, &recordError{path + ": cnf has no jwk"}
		}
		xy, err := pointOf(r.Cnf.JWK)
		if err != nil {
			return nil, &recordError{path + ": " + err.Error()}
		}
		if err := requireOnCurveP256(xy); err != nil {
			return nil, &recordError{path + ": cnf.jwk is not a P-256 point"}
		}
	}
	return &r, nil
}

func pointOf(k *ecJWK) ([]byte, error) {
	if k.Kty != "EC" || k.Crv != "P-256" {
		return nil, fmt.Errorf("cnf.jwk is %s %s, want EC P-256", k.Kty, k.Crv)
	}
	x, errX := base64.RawURLEncoding.DecodeString(k.X)
	y, errY := base64.RawURLEncoding.DecodeString(k.Y)
	if errX != nil || errY != nil || len(x) != 32 || len(y) != 32 {
		return nil, errors.New("cnf.jwk x and y are not 32 bytes each, base64url")
	}
	return append(x, y...), nil
}

// writeRecord replaces path whole, through a rename in its own directory, so
// a crash leaves the old record or the new one and never half of either.
func writeRecord(path string, r *provRecord) error {
	raw, err := json.MarshalIndent(r, "", "  ")
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(path), ".tfwprov-record-*")
	if err != nil {
		return fmt.Errorf("--record: %w", err)
	}
	tmp := f.Name()
	_, werr := f.Write(append(raw, '\n'))
	cerr := f.Close()
	if err := errors.Join(werr, cerr); err != nil {
		_ = os.Remove(tmp)
		return fmt.Errorf("--record: %w", err)
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return fmt.Errorf("--record: %w", err)
	}
	return nil
}

// recordSerial is --bake-eeprom's half: it starts the record with the UEID,
// or finds the same UEID already there and leaves the record as it is. A
// record of another serial is another device's, and is refused.
func recordSerial(path string, serial [tfw.SerialLen]byte) error {
	r, err := readRecord(path)
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return writeRecord(path, &provRecord{UEID: ueidOf(serial)})
	case err != nil:
		return err
	case r.UEID != ueidOf(serial):
		return &recordError{fmt.Sprintf("%s is the record of ueid %s, not of this serial; not overwritten", path, r.UEID)}
	}
	return nil
}

// recordReadyForPoint is checked before --commit touches the element: the
// record must exist, from --bake-eeprom, and hold no point yet. A point
// already there is a device already committed, and a new key would never be
// that device's.
func recordReadyForPoint(path string) error {
	r, err := readRecord(path)
	if err != nil {
		return err
	}
	if r.Cnf != nil {
		return &recordError{path + " already holds a DeviceID point; --commit would generate another device's key"}
	}
	return nil
}

// recordPoint is --commit's and --verify's half: it joins the element's
// DeviceID point to the record's UEID. The same point already there is the
// same device; a different point is a different device, and is refused.
func recordPoint(path string, xy []byte) error {
	r, err := readRecord(path)
	if err != nil {
		return err
	}
	k, err := jwkOf(xy)
	if err != nil {
		return err
	}
	if r.Cnf != nil {
		if *r.Cnf.JWK != *k {
			return &recordError{path + " holds a different DeviceID point: this element is a different device; not overwritten"}
		}
		return nil
	}
	r.Cnf = &confirmation{JWK: k}
	return writeRecord(path, r)
}
