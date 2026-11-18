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
