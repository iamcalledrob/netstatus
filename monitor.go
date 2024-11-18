package netstatus

import (
	"context"
	"fmt"
)

type Status struct {
	Available bool
	Kind      InterfaceKind
	// More in future, e.g. ssid, IsPublicNetwork etc.
}

func (s Status) String() string {
	return fmt.Sprintf("Available: %t, Kind: %s", s.Available, s.Kind)
}

type InterfaceKind string

const (
	InterfaceTypeUnknown  InterfaceKind = "unknown"
	InterfaceTypeWired                  = "wired"
	InterfaceTypeWifi                   = "wifi"
	InterfaceTypeCellular               = "cellular"
)

type Monitor struct {
	*monitor // provided by cgo implementations
}

// StartMonitor starts monitoring the network status of the current device. This can then be
// queried at any time using Monitor.Current.
func StartMonitor(ctx context.Context) *Monitor {
	return &Monitor{startMonitor(ctx)}
}

// OnChange registers a callback to be invoked when the network status changes.
// Do not call Monitor.Current inside the OnChange callback--this will result in deadlock.
func (m *Monitor) OnChange(cb func(Status)) {
	m.monitor.OnChange(cb)
}

// Current returns the current network status of the device.
func (m *Monitor) Current(ctx context.Context) Status {
	return m.monitor.Current(ctx)
}
