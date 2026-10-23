// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of go-xkms.
//
// go-xkms is dual-licensed:
//
// 1. GNU Affero General Public License v3.0 (AGPL-3.0)
//    See LICENSE file or visit https://www.gnu.org/licenses/agpl-3.0.html
//
// 2. Commercial License
//    Contact licensing@automatethethings.com for commercial licensing options.

package avr

import (
	"errors"
	"io"
	"log/slog"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Validate fills only what was left unset: a logger and a positive deadline
// the caller chose are theirs and survive validation unchanged.
func TestValidateKeepsTheCallersLoggerAndTimeout(t *testing.T) {
	logger := slog.New(slog.NewTextHandler(io.Discard, nil))
	cfg := DefaultConfig()
	cfg.Logger = logger
	cfg.RequestTimeout = 7 * time.Second

	require.NoError(t, cfg.Validate())
	assert.Same(t, logger, cfg.Logger, "a logger the caller set is never replaced")
	assert.Equal(t, 7*time.Second, cfg.RequestTimeout, "a positive deadline is never replaced")
}

// Every sentinel is its own fact. Two names for one error would let a caller
// branch on the one that never fires, and errors.Is cannot tell them apart.
func TestSentinelsAreDistinct(t *testing.T) {
	sentinels := map[string]error{
		"ErrNilConfig":             ErrNilConfig,
		"ErrNilDevice":             ErrNilDevice,
		"ErrInvalidFactoryConfig":  ErrInvalidFactoryConfig,
		"ErrMissingKeyCN":          ErrMissingKeyCN,
		"ErrInvalidSlot":           ErrInvalidSlot,
		"ErrBackendClosed":         ErrBackendClosed,
		"ErrInvalidKeyAttributes":  ErrInvalidKeyAttributes,
		"ErrUnknownKey":            ErrUnknownKey,
		"ErrInvalidNonce":          ErrInvalidNonce,
		"ErrTrustStoreInit":        ErrTrustStoreInit,
		"ErrSigningUnsupported":    ErrSigningUnsupported,
		"ErrInvalidAttestation":    ErrInvalidAttestation,
		"ErrPossessionMismatch":    ErrPossessionMismatch,
		"ErrDecryptionUnsupported": ErrDecryptionUnsupported,
		"ErrGenerationUnsupported": ErrGenerationUnsupported,
		"ErrDeletionUnsupported":   ErrDeletionUnsupported,
		"ErrRotationUnsupported":   ErrRotationUnsupported,
		"ErrMalformedCertificate":  ErrMalformedCertificate,
		"ErrProtocolVersion":       ErrProtocolVersion,
		"ErrUnknownCryptoTier":     ErrUnknownCryptoTier,
		"ErrNoDeviceKey":           ErrNoDeviceKey,
		"ErrInvalidDevicePoint":    ErrInvalidDevicePoint,
		"ErrDeviceUnreachable":     ErrDeviceUnreachable,
		"ErrDeviceProtocol":        ErrDeviceProtocol,
		"ErrDeviceRefused":         ErrDeviceRefused,
		"ErrCommandUnavailable":    ErrCommandUnavailable,
		"ErrNotProvisioned":        ErrNotProvisioned,
		"ErrDeviceCrypto":          ErrDeviceCrypto,
		"ErrAuthRequired":          ErrAuthRequired,
		"ErrDeviceBusy":            ErrDeviceBusy,
		"ErrProtectedRegion":       ErrProtectedRegion,
		"ErrDeviceInternal":        ErrDeviceInternal,
		"ErrUnsupportedByDevice":   ErrUnsupportedByDevice,
		"ErrElementUnreachable":    ErrElementUnreachable,
		"ErrInvalidArgument":       ErrInvalidArgument,
	}
	messages := make(map[string]string, len(sentinels))
	for name, err := range sentinels {
		require.NotNil(t, err, name)
		assert.True(t, strings.HasPrefix(err.Error(), "avr backend: "), "%s: %q", name, err)
		if prev, dup := messages[err.Error()]; dup {
			t.Errorf("%s and %s carry one message", prev, name)
		}
		messages[err.Error()] = name
		for other, oerr := range sentinels {
			if other != name {
				assert.False(t, errors.Is(err, oerr), "%s is %s", name, other)
			}
		}
	}
}
