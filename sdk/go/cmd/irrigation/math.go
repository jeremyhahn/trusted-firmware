// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import "math"

// exp exists so the VPD formula reads like the equation it is. math is stdlib,
// so it costs the module nothing -- and sdk/go's dependency list is the one
// thing about it that must not grow.
func exp(x float64) float64 { return math.Exp(x) }
