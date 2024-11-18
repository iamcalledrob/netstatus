package netstatus

import (
	"context"
	"testing"
	"time"
)

// TestCurrentNotBlocking tests for no panics/build issues, but does not test much of the underlying behaviour
// (which is difficult or impossible to test).
func TestCurrentNotBlocking(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	m := StartMonitor(ctx)

	currentReturned := make(chan struct{})
	go func() {
		m.Current(context.Background())
		close(currentReturned)
	}()

	select {
	case <-currentReturned:
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("m.Current() didn't return")
	}
}
