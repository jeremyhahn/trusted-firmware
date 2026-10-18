package tfwsim

import (
	"errors"
	"fmt"
	"net"
	"testing"
)

type logTB struct {
	testing.TB
	logs []string
}

func (l *logTB) Logf(format string, args ...any) {
	l.logs = append(l.logs, fmt.Sprintf(format, args...))
}

// A close that fails is logged, never fatal; one that fails only because the
// other side already closed it is the expected end of a test and says nothing.
func TestLogCloseReportsOnlyAnUnexpectedFailure(t *testing.T) {
	tb := &logTB{TB: t}
	logClose(tb, "listener", nil)
	logClose(tb, "listener", fmt.Errorf("wrapped: %w", net.ErrClosed))
	logClose(tb, "ethernet link", errors.New("connection reset"))
	want := []string{"tfwsim: closing the ethernet link: connection reset"}
	if len(tb.logs) != len(want) || tb.logs[0] != want[0] {
		t.Fatalf("logged %q, want %q", tb.logs, want)
	}
}
