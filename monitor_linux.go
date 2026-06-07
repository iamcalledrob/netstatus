//go:build cgo && linux

package netstatus

/*
#include "monitor_linux.h"

extern void linux_invoke_callback(uintptr_t hnd, int available, int kind);
*/
import "C"
import (
	"context"
	"sync"
	"runtime/cgo"
)

const (
	cKindUnknown  = 0
	cKindWired    = 1
	cKindWifi     = 2
	cKindCellular = 3
)

type monitor struct {
	rcvd       chan struct{}
	rcvdClosed bool // guards against double-close; accessed under mu

	mu       sync.Mutex
	last     *Status
	onChange func(Status)

	sock C.int
}

func startMonitor(ctx context.Context) *monitor {
	m := &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
		sock:     -1,
	}

	cbHnd := cgo.NewHandle(m.handleCallback)

	sock := C.linux_start_monitor(C.uintptr_t(cbHnd))
	m.sock = sock

	go func() {
		<-ctx.Done()
		C.linux_stop_monitor(m.sock)
		cbHnd.Delete()

		m.mu.Lock()
		defer m.mu.Unlock()
		if m.last == nil && !m.rcvdClosed {
			m.rcvdClosed = true
			close(m.rcvd)
		}
	}()

	return m
}

func (m *monitor) handleCallback(available C.int, kind C.int) {
	m.handleStatus(available != 0, int(kind))
}

func (m *monitor) handleStatus(available bool, kind int) {
	s := Status{
		Available: available,
		Kind:      cKindToInterfaceKind(C.int(kind)),
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	var changed bool
	if m.last == nil && !m.rcvdClosed {
		m.rcvdClosed = true
		close(m.rcvd)
	} else if m.last != nil && *m.last != s {
		changed = true
	}
	m.last = &s
	if changed {
		m.onChange(s)
	}
}

func (m *monitor) OnChange(cb func(Status)) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.onChange = cb
}

func (m *monitor) Current(ctx context.Context) Status {
	select {
	case <-m.rcvd:
	case <-ctx.Done():
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	if m.last == nil {
		return Status{}
	}
	return *m.last
}

func cKindToInterfaceKind(k C.int) InterfaceKind {
	switch int(k) {
	case cKindWired:
		return InterfaceTypeWired
	case cKindWifi:
		return InterfaceTypeWifi
	case cKindCellular:
		return InterfaceTypeCellular
	default:
		return InterfaceTypeUnknown
	}
}

// linux_invoke_callback is called from C when the network status changes.
//
//export linux_invoke_callback
func linux_invoke_callback(hnd C.uintptr_t, available C.int, kind C.int) {
	cgo.Handle(hnd).Value().(func(C.int, C.int))(available, kind)
}
