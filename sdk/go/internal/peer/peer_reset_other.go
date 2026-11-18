//go:build !unix

package peer

import "os/exec"

// killHookGroup has no process group to set up here: the context's cancel
// kills the hook itself, and WaitDelay bounds the wait for its output.
func killHookGroup(*exec.Cmd) {}
