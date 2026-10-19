// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"crypto/x509"
	"encoding/asn1"
	"errors"
)

// ApplicationEvidenceLen is the width of the value in the measurements claim
// (273): one sha-256 hash-entry, 32 opaque bytes. The key, the width and the
// meaning are go-xkms's dice-device-identity row; this module only reads them.
const ApplicationEvidenceLen = 32

// oidUCCSEvidence is tcg-dice-UCCS-evidence, TCG DICE Attestation
// Architecture r23: the alias certificate's one extension.
var oidUCCSEvidence = asn1.ObjectIdentifier{2, 23, 133, 5, 4, 6}

// EvidenceError reports a certificate or claims set that is not the shape the
// device writes (docs/dice-attestation/api.md). It matches ErrBadFormat.
type EvidenceError struct {
	Reason string
}

func (e *EvidenceError) Error() string { return "tfw: malformed evidence: " + e.Reason }

func (e *EvidenceError) Is(target error) bool { return target == ErrBadFormat }

func evidenceErr(reason string) error { return &EvidenceError{Reason: reason} }

// AliasUCCS returns the claims set the alias certificate der carries in its
// tcg-dice-UCCS-evidence extension (UccsEvidence ::= SEQUENCE { uccs OCTET
// STRING }). It checks the shape only: the certificate's signature, and so
// whether the claims are the device's, is go-xkms's to verify.
func AliasUCCS(der []byte) ([]byte, error) {
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		return nil, errors.Join(evidenceErr("not a certificate"), err)
	}
	for _, ext := range cert.Extensions {
		if !ext.Id.Equal(oidUCCSEvidence) {
			continue
		}
		var ev struct{ UCCS []byte }
		rest, err := asn1.Unmarshal(ext.Value, &ev)
		if err != nil {
			return nil, errors.Join(evidenceErr("tcg-dice-UCCS-evidence is not a UccsEvidence"), err)
		}
		if len(rest) != 0 {
			return nil, evidenceErr("trailing bytes after the UccsEvidence")
		}
		return ev.UCCS, nil
	}
	return nil, evidenceErr("no tcg-dice-UCCS-evidence extension")
}

// ApplicationEvidence returns the 32 bytes in the measurements claim (273) of
// the claims set uccs: the value of the one sha-256 hash-entry of the one file,
// "app", in the evidence CoSWID of the one [258, CoSWID] entry.
//
// The bytes are opaque. Which bytes they are is the device's choice: those a
// running application supplied through tfw_app_evidence, or, where none did,
// the firmware's own measurement, so that 273 is never absent and never zeros.
// What they mean is decided by matching them against a registered reference,
// which is go-xkms's. Any other width, and any other shape, is an
// *EvidenceError: there is no shorter or longer evidence to fall back on.
func ApplicationEvidence(uccs []byte) ([ApplicationEvidenceLen]byte, error) {
	var out [ApplicationEvidenceLen]byte

	v, err := cborDecode(uccs)
	if err != nil {
		return out, evidenceErr("claims set: " + err.Error())
	}
	claims, ok := v.(map[uint64]any)
	if !ok {
		return out, evidenceErr("claims set is not a map")
	}
	meas, ok := claims[273]
	if !ok {
		return out, evidenceErr("no measurements claim (273)")
	}
	entries, ok := meas.([]any)
	if !ok || len(entries) != 1 {
		return out, evidenceErr("measurements claim is not exactly one entry")
	}
	pair, ok := entries[0].([]any)
	if !ok || len(pair) != 2 {
		return out, evidenceErr("measurement is not a [content-type, content] pair")
	}
	if ct, ok := pair[0].(uint64); !ok || ct != 258 {
		return out, evidenceErr("measurement content type is not 258, application/swid+cbor")
	}
	tag, ok := pair[1].([]byte)
	if !ok {
		return out, evidenceErr("measurement content is not a byte string")
	}
	v, err = cborDecode(tag)
	if err != nil {
		return out, evidenceErr("CoSWID: " + err.Error())
	}
	coswid, ok := v.(map[uint64]any)
	if !ok {
		return out, evidenceErr("CoSWID is not a map")
	}
	evidence, ok := coswid[3].(map[uint64]any)
	if !ok {
		return out, evidenceErr("CoSWID has no evidence map (3)")
	}
	file, ok := evidence[17].(map[uint64]any)
	if !ok || len(evidence) != 1 {
		return out, evidenceErr("evidence is not exactly one file (17)")
	}
	if name, ok := file[24].(string); !ok || name != "app" || len(file) != 2 {
		return out, evidenceErr(`file is not exactly fs-name (24) "app" and a hash (7)`)
	}
	hash, ok := file[7].([]any)
	if !ok || len(hash) != 2 {
		return out, evidenceErr("file hash is not a [hash-alg-id, hash-value] entry")
	}
	if alg, ok := hash[0].(uint64); !ok || alg != 1 {
		return out, evidenceErr("file hash-alg-id is not 1, sha-256")
	}
	value, ok := hash[1].([]byte)
	if !ok || len(value) != ApplicationEvidenceLen {
		return out, evidenceErr("file hash-value is not 32 bytes")
	}
	copy(out[:], value)
	return out, nil
}
