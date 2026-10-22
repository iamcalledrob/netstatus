//go:build cgo

package netstatus

/*
#cgo CFLAGS: -x objective-c -Wno-incompatible-pointer-types
#cgo LDFLAGS: -framework Foundation -framework Network
#import <Foundation/Foundation.h>
#import <Network/Network.h>

extern void invoke_callback(uintptr_t hnd, nw_path_t path);
static void set_update_handler(nw_path_monitor_t monitor, uintptr_t cb_hnd) {
	nw_path_monitor_set_update_handler(monitor, ^(nw_path_t path) {
		// The docs say retain/release are needed, though other implementations don't do so?
		nw_retain(path);
		invoke_callback(cb_hnd, path);
		nw_release(path);
	});
}

extern void invoke_cancel(uintptr_t hnd);
static void set_cancel_handler(nw_path_monitor_t monitor, uintptr_t cb_hnd) {
	// Capture the monitor as an integer so the block does not retain it and
	// create a cycle. The reference returned by nw_path_monitor_create is
	// released once native cancellation has completed.
	uintptr_t monitor_hnd = (uintptr_t)monitor;
	nw_path_monitor_set_cancel_handler(monitor, ^{
		invoke_cancel(cb_hnd);
		nw_release((nw_path_monitor_t)monitor_hnd);
	});
}
*/
import "C"
import (
	"context"
	"fmt"
	"runtime/cgo"
	"sync"
)

type monitor struct {
	rcvd     chan struct{}
	rcvdOnce sync.Once

	callbackMu sync.Mutex
	mu         sync.Mutex
	last       *Status
	onChange   func(Status)
}

func startMonitor(ctx context.Context) *monitor {
	mon := C.nw_path_monitor_create()
	if mon == nil {
		// This should never happen®. The docs say this will only fail due to bad arguments.
		panic(fmt.Sprintf("nw_path_monitor_create: %v", mon))
	}
	m := &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
	}
	// The initial callback won't be fired if the queue isn't set.
	// Using the main queue results in deadlock--don't do it!
	C.nw_path_monitor_set_queue(mon, C.dispatch_get_global_queue(C.QOS_CLASS_DEFAULT, 0))

	// Use a cgo.Handle to give C an opaque, unique callback ID. Callbacks resolve
	// the monitor through callbacks rather than Handle.Value: the cancel handler
	// may delete the handle while an update is already queued on the dispatch
	// queue, and using a deleted handle panics.
	cbHnd := cgo.NewHandle(m)
	callbacksMu.Lock()
	callbacks[C.uintptr_t(cbHnd)] = m
	callbacksMu.Unlock()
	C.set_update_handler(mon, C.uintptr_t(cbHnd))
	C.set_cancel_handler(mon, C.uintptr_t(cbHnd))

	// The callback should get fired immediately with the current state, as per the docs
	// in path_monitor.h for nw_path_monitor_set_update_handler
	C.nw_path_monitor_start(mon)

	go func() {
		<-ctx.Done()
		C.nw_path_monitor_cancel(mon)

		m.mu.Lock()
		defer m.mu.Unlock()
		if m.last == nil {
			m.signalReceived()
		}
	}()

	return m
}

func (m *monitor) rawCallback(path C.nw_path_t) {
	m.update(makeStatus(path))
}

func (m *monitor) update(status Status) {
	// Serialize native updates while allowing Current and OnChange to be called
	// safely from the user callback.
	m.callbackMu.Lock()
	defer m.callbackMu.Unlock()

	m.mu.Lock()

	var changed bool
	if m.last == nil {
		m.signalReceived()
	} else if *m.last != status {
		changed = true
	}

	m.last = &status
	cb := m.onChange
	m.mu.Unlock()

	// Only fire callback if the status actually changed
	if changed {
		cb(status)
	}
}

func (m *monitor) signalReceived() {
	m.rcvdOnce.Do(func() { close(m.rcvd) })
}

func (m *monitor) OnChange(cb func(Status)) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.onChange = cb
}

func (m *monitor) Current(ctx context.Context) Status {
	// Wait until the callback is triggered. This should happen near-instantaneously.
	// Ctx to allow cancellation in case it doesn't.
	select {
	case <-m.rcvd:
	case <-ctx.Done():
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	// This would happen if StartMonitor was immediately followed with Close before any values were received
	if m.last == nil {
		return Status{}
	}

	return *m.last
}

func makeStatus(path C.nw_path_t) Status {
	kind := InterfaceTypeUnknown
	if bool(C.nw_path_is_expensive(path)) {
		// Tethering: interface type may be Wifi or Wired, but is ultimately Cellular.
		kind = InterfaceTypeCellular
	} else if bool(C.nw_path_uses_interface_type(path, C.nw_interface_type_cellular)) {
		kind = InterfaceTypeCellular
	} else if bool(C.nw_path_uses_interface_type(path, C.nw_interface_type_wifi)) {
		kind = InterfaceTypeWifi
	} else if bool(C.nw_path_uses_interface_type(path, C.nw_interface_type_wired)) {
		kind = InterfaceTypeWired
	}
	s := C.nw_path_get_status(path)
	return Status{
		Available: s == C.nw_path_status_satisfied || s == C.nw_path_status_satisfiable,
		Kind:      kind,
	}
}

var callbacksMu sync.Mutex
var callbacks = map[C.uintptr_t]*monitor{}

// Invokes the callback identified by hnd. Used to provide a C-exported function that can safely
// ignore an update delivered after cancellation.
//
//export invoke_callback
func invoke_callback(hnd C.uintptr_t, path C.nw_path_t) {
	callbacksMu.Lock()
	m := callbacks[hnd]
	callbacksMu.Unlock()

	if m != nil {
		m.rawCallback(path)
	}
}

//export invoke_cancel
func invoke_cancel(hnd C.uintptr_t) {
	callbacksMu.Lock()
	_, ok := callbacks[hnd]
	if ok {
		delete(callbacks, hnd)
	}
	callbacksMu.Unlock()

	// Guard against an unexpected duplicate native cancellation callback.
	if ok {
		cgo.Handle(hnd).Delete()
	}
}
