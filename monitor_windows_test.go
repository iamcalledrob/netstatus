//go:build windows && cgo

package netstatus

import (
	"context"
	"testing"
	"time"
)

func TestImmediateCancellationReturns(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	m := StartMonitor(ctx)
	done := make(chan struct{})
	go func() {
		m.Current(context.Background())
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("monitor did not stop after immediate cancellation")
	}
}

func TestOnChangeCanAccessMonitor(t *testing.T) {
	m := &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
	}
	called := make(chan struct{})
	m.OnChange(func(Status) {
		m.Current(context.Background())
		m.OnChange(func(Status) {})
		close(called)
	})

	done := make(chan struct{})
	go func() {
		m.rawCallback(false)
		m.rawCallback(true)
		close(done)
	}()

	select {
	case <-called:
	case <-time.After(time.Second):
		t.Fatal("change callback deadlocked while accessing the monitor")
	}

	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("status updates did not finish")
	}
}
