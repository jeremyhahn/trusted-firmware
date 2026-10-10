// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfw

import (
	"context"
	"fmt"
)

// CmdAttestCSR is ATTEST_CSR: request [b:32], response [sig:64], issue #30.
// Its feature bit is FeatureAttestCSR.
const CmdAttestCSR Cmd = 0x64

// CSRSigLen is the length of the ATTEST_CSR response: a P-256 signature as raw
// r||s, 32 bytes each (TFW_SIG_LEN).
const CSRSigLen = ecdsaRawSigLen

// AttestCSR asks the device for the possession signature that ACME finalize
// needs, and returns it as the 64 raw bytes r||s the device sent.
//
// b is the same 32 bytes handed to AttestCert for the challenge,
// SHA-256(keyAuthorization). The device signs a CertificationRequestInfo it
// fills from its own state, so the host chooses the nonce and nothing else.
//
// This is transport only. The CertificationRequestInfo, the CSR built around the
// signature and the check of it against the DeviceID key are certificate-profile
// work, which this repo does not own: go-xkms reproduces the 365-byte
// CertificationRequestInfo (go-xkms#1441) and verifies the signature over it. The
// bytes the device emits are pinned on the firmware side by TFWV_CSR_CRI and
// TFWV_CSR_CRI_DIGEST in testdata/vectors.h.
//
// Returns StatusErrUnsupported, wrapped, when the device's CAPS do not report
// FeatureAttestCSR -- before ATTEST_CSR is sent.
func (d *Device) AttestCSR(ctx context.Context, b [AliasNonceLen]byte) ([CSRSigLen]byte, error) {
	var sig [CSRSigLen]byte

	c, err := d.Caps(ctx)
	if err != nil {
		return sig, err
	}
	if !c.Has(FeatureAttestCSR) {
		return sig, fmt.Errorf("tfw: ATTEST_CSR: the device does not report the feature: %w", StatusErrUnsupported)
	}

	f, err := d.Do(ctx, CmdAttestCSR, b[:])
	if err != nil {
		return sig, err
	}
	if len(f.Payload) != ecdsaRawSigLen {
		return sig, &ShortReplyError{Cmd: CmdAttestCSR, Got: len(f.Payload), Need: ecdsaRawSigLen}
	}
	copy(sig[:], f.Payload)
	return sig, nil
}
