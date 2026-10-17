// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"encoding/hex"
	"fmt"
	"os"
	"strings"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// otaSigUsage names the two ways to give the row key's authorization (#137,
// docs/ota/api.md, "Authorization by the row key").
const otaSigUsage = "--sig=<128 hex digits> or --sig-file=<path>"

// takeOTASig removes --sig=<hex> or --sig-file=<path> from args and returns the
// 64-byte ES256 r||s it names. A file holds the 64 raw bytes or their hex.
// tfwctl signs nothing: the signature comes from the row key's holder.
func takeOTASig(args []string) (sig [tfw.OTAAuthSigLen]byte, rest []string, found bool, err error) {
	for _, a := range args {
		var text string
		switch {
		case strings.HasPrefix(a, "--sig="):
			text = strings.TrimPrefix(a, "--sig=")
		case strings.HasPrefix(a, "--sig-file="):
			raw, rerr := os.ReadFile(strings.TrimPrefix(a, "--sig-file="))
			if rerr != nil {
				return sig, nil, false, fmt.Errorf("%s: %w", a, rerr)
			}
			if len(raw) == tfw.OTAAuthSigLen {
				text = hex.EncodeToString(raw)
			} else {
				text = string(raw)
			}
		default:
			rest = append(rest, a)
			continue
		}
		if found {
			return sig, nil, false, fmt.Errorf("give the authorization once: %s", otaSigUsage)
		}
		b, derr := hex.DecodeString(strings.TrimSpace(text))
		if derr != nil || len(b) != tfw.OTAAuthSigLen {
			return sig, nil, false, fmt.Errorf("the authorization is %d bytes: %s", tfw.OTAAuthSigLen, otaSigUsage)
		}
		copy(sig[:], b)
		found = true
	}
	return sig, rest, found, nil
}

// needOTASig is the refusal before anything is sent: since #137 the device
// answers OTA_ACTIVATE and OTA_CONFIRM with TFW_ERR_AUTH_REQUIRED without one.
func needOTASig(verb string) error {
	return fmt.Errorf("%s needs the row key's authorization, sent as OTA_AUTH first: %s", verb, otaSigUsage)
}
