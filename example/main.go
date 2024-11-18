package main

import (
	"context"
	"fmt"
	"github.com/iamcalledrob/netstatus"
)

func main() {
	ctx, cancelCtx := context.WithCancel(context.Background())
	defer cancelCtx()

	m := netstatus.StartMonitor(ctx)

	fmt.Printf("Current status: %s\n", m.Current(ctx))

	m.OnChange(func(s netstatus.Status) {
		if s.Available {
			fmt.Printf("Internet available (kind: %s)\n", s.Kind)
		} else {
			fmt.Printf("Internet unavailable\n")
		}
	})

	select {}
}
