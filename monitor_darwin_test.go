//go:build darwin && cgo

package netstatus

import (
	"context"
	"sync"
	"testing"
	"time"
)

func newTestMonitor() *monitor {
	return &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
	}
}

func TestSignalReceivedCanBeCalledConcurrently(t *testing.T) {
	m := newTestMonitor()

	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			m.signalReceived()
		}()
	}
	wg.Wait()

	select {
	case <-m.rcvd:
	default:
		t.Fatal("received channel was not closed")
	}
}

func TestOnChangeCanAccessMonitor(t *testing.T) {
	m := newTestMonitor()
	called := make(chan struct{})
	m.OnChange(func(Status) {
		m.Current(context.Background())
		m.OnChange(func(Status) {})
		close(called)
	})

	done := make(chan struct{})
	go func() {
		m.update(Status{Available: false})
		m.update(Status{Available: true, Kind: InterfaceTypeWifi})
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
