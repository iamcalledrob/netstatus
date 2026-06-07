//go:build cgo && linux

package netstatus

import (
	"testing"
)

// newTestMonitor creates a monitor without starting the netlink thread, for unit testing
// the Go-side callback logic in isolation.
func newTestMonitor() *monitor {
	return &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
	}
}

func TestHandleCallback_InitialCallSignalsReady(t *testing.T) {
	m := newTestMonitor()
	m.handleStatus(true, cKindWifi)

	select {
	case <-m.rcvd:
	default:
		t.Fatal("rcvd not closed after initial callback")
	}
	if m.last == nil || !m.last.Available || m.last.Kind != InterfaceTypeWifi {
		t.Fatalf("unexpected last status: %+v", m.last)
	}
}

func TestHandleCallback_OnChangeNotFiredOnInitial(t *testing.T) {
	m := newTestMonitor()
	fired := false
	m.onChange = func(Status) { fired = true }
	m.handleStatus(true, cKindWifi)
	if fired {
		t.Fatal("onChange fired on initial callback, want no-op")
	}
}

func TestHandleCallback_OnChangeFiredOnAvailabilityChange(t *testing.T) {
	m := newTestMonitor()
	m.handleStatus(true, cKindWifi) // initial

	var got Status
	m.onChange = func(s Status) { got = s }
	m.handleStatus(false, cKindUnknown)

	if got.Available {
		t.Fatal("expected available=false after wifi off")
	}
}

func TestHandleCallback_OnChangeFiredOnKindChange(t *testing.T) {
	m := newTestMonitor()
	m.handleStatus(true, cKindWifi) // initial: wifi

	var got Status
	m.onChange = func(s Status) { got = s }
	m.handleStatus(true, cKindWired) // same available, different kind

	if got.Kind != InterfaceTypeWired {
		t.Fatalf("expected kind=wired after kind change, got %q", got.Kind)
	}
}

func TestHandleCallback_OnChangeNotFiredWhenUnchanged(t *testing.T) {
	m := newTestMonitor()
	m.handleStatus(true, cKindWifi) // initial

	fired := false
	m.onChange = func(Status) { fired = true }
	m.handleStatus(true, cKindWifi) // identical — should not fire

	if fired {
		t.Fatal("onChange fired with unchanged status")
	}
}

func TestHandleCallback_RcvdNotDoubledClosed(t *testing.T) {
	m := newTestMonitor()
	m.handleStatus(true, cKindWifi)
	// Second call must not panic with double-close.
	m.handleStatus(false, cKindUnknown)
}
