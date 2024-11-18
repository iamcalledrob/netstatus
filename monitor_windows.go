//go:build cgo

package netstatus

/*
#cgo LDFLAGS: -lstdc++ -lole32
#import "monitor_windows.hpp"

// for Windows, cgo exports with __declspec(dllexport), and this is needed for this forward declaration to be valid.
extern __declspec(dllexport) void universal_callback(CSMHandle hnd, _Bool isConnected);

static CSMHandle ConnectionStatusMonitorCreateWithUniversalCallback() {
	return ConnectionStatusMonitorCreate(&universal_callback);
}

*/
import "C"
import (
	"context"
	"runtime"
	"sync"
)

type monitor struct {
	rcvd chan struct{}

	mu       sync.Mutex
	last     *Status
	onChange func(Status)
}

func startMonitor(ctx context.Context) *monitor {
	handle := C.ConnectionStatusMonitorCreateWithUniversalCallback()

	m := &monitor{
		rcvd:     make(chan struct{}),
		onChange: func(Status) {},
	}

	callbacksMu.Lock()
	callbacks[handle] = m.rawCallback
	callbacksMu.Unlock()

	stopped := make(chan struct{})

	go func() {
		// Thread state is modified by the ConnectionStatusMonitor in two ways:
		//  (1) CoInitialize() is invoked on this thread
		//  (2) A message pump is set up on this thread
		//
		// Locking the thread is required to ensure that other goroutines can't interfere and don't inherit a thread
		// in an unusual state. Thread state is restored up before ConnectionStatusMonitorStart returns.
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()

		C.ConnectionStatusMonitorStart(handle)

		// monitor can now be freed
		close(stopped)
	}()

	go func() {
		<-ctx.Done()
		C.ConnectionStatusMonitorStop(handle)

		<-stopped
		C.ConnectionStatusMonitorFree(handle)

		callbacksMu.Lock()
		delete(callbacks, handle)
		callbacksMu.Unlock()

		m.mu.Lock()
		defer m.mu.Unlock()
		if m.last == nil {
			close(m.rcvd)
		}
	}()

	return m
}

func (m *monitor) rawCallback(isConnected bool) {
	status := makeStatus(isConnected)

	m.mu.Lock()
	defer m.mu.Unlock()

	// Initial received state shouldn't count as a change.
	var changed bool
	if m.last == nil {
		close(m.rcvd)
	} else if *m.last != status {
		changed = true
	}

	m.last = &status

	// Only fire onChange if the status actually changed
	if changed {
		m.onChange(status)
	}
}

func (m *monitor) OnChange(cb func(status Status)) {
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

func makeStatus(isConnected bool) Status {
	// Wired/Wireless/Cellular info is extra work to query on Windows, so skipping its inclusion for now.
	// For cost (cellular/roaming): https://learn.microsoft.com/en-us/windows/win32/api/netlistmgr/nn-netlistmgr-inetworkcostmanager
	// For wifi vs. wired, it may be necessary to query WMI or use WinRT, e.g.
	// Windows.Networking.Connectivity.NetworkInformation.GetInternetConnectionProfile()
	// -- ideally WinRT could be avoided so cross-compilation remains easy.
	return Status{
		Available: isConnected,
		Kind:      InterfaceTypeUnknown,
	}
}

// Easiest way for C to call back into a specific monitor instance is to use a common, universall C callback,
// then handle the instance mapping in Go.
var callbacksMu sync.Mutex
var callbacks = map[C.CSMHandle]func(bool){}

// Mapping to the right callback (i.e. of the right monitor) is done here in Go to simplify the approach in C,
// because a lack of lambdas/currying makes registering different callbacks more awkward.
//
//export universal_callback
func universal_callback(hnd C.CSMHandle, isConnected C.bool) {
	callbacksMu.Lock()
	cb, ok := callbacks[hnd]
	callbacksMu.Unlock()

	if ok {
		cb(bool(isConnected))
	}
}
