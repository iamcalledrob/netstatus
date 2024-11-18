//go:build !darwin && !windows

package netstatus

import "context"

// Noop implementation of monitor for unsupported platforms.

type monitor struct{}

func startMonitor(context.Context) *monitor {
	return &monitor{}
}

func (m *monitor) OnChange(func(Status)) {}

func (m *monitor) Current(ctx context.Context) Status {
	return Status{
		Available: true,
		Kind:      InterfaceTypeUnknown,
	}
}
