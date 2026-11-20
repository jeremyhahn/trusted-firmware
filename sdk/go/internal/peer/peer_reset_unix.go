//go:build unix

package peer

import (
	"errors"
	"os"
	"os/exec"
	"syscall"
)

// killHookGroup starts the hook in its own process group and makes the
// context's cancel kill that whole group, so a child the hook started dies
// with it instead of outliving the bound.
func killHookGroup(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error {
		err := syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
		if errors.Is(err, syscall.ESRCH) {
			return os.ErrProcessDone
		}
		return err
	}
}

// endHookGroup kills whatever is left of the hook's process group once the
// hook has returned: a child the hook backgrounded outlives a hook that
// exits first, and Cancel never runs for a parent already gone (Codex on
// #568).
func endHookGroup(cmd *exec.Cmd) {
	if cmd.Process == nil {
		return
	}
	_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL) // ESRCH: the group is already gone
}
