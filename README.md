# netstatus - Network Status Monitor
`netstatus` allows you to check and monitor the internet connectivity status of the host operating
system using OS native APIs (via cgo).

Supported on iOS, macOS and Windows. Noop on other platforms - feel free to contribute.

## Why is this useful?
When internet connectivity is lost, e.g. due to a switch between WiFi networks, sockets IO ops may
not pick up on this in a timely way. Heartbeats can be used to pick up on failure more quickly, but
typically these will need a long (30+s) timeout, delaying detection of the failure.

Instead, `netstatus` makes it possible to monitor for the state of the device's internet
connectivity, allowing you to proactively close connections that are destined to time-out anyway,
and to attempt reconnection as soon as connectivity is regained.

In practice, the OS-provided internet connectivity status proves to be very accurate.

## Goals
- Be minimal - take no external dependencies
- Be easy to (cross) compile using zig cc/zig c++

## Compilation
This package requires cgo.
The easiest way to compile is using zig, for example, to build for the current platform:
```sh
CGO_ENABLED=1 "CC=zig cc" "CXX=zig c++" go build example/main.go
```
Or to cross-compile, pick a suitable GOOS/GOARCH/zig -target:
```sh
CGO_ENABLED=1 GOOS=windows GOARCH=amd64 "CC=zig cc -target x86_64-windows-gnu" "CXX=zig c++ -target x86_64-windows-gnu" go build example/main.go
```


## Usage
[Godoc](http://pkg.go.dev/github.com/iamcalledrob/netstatus)

```go
m := netstatus.StartMonitor(ctx)

fmt.Printf("Current status: %s\n", m.Current(ctx))

m.OnChange(func(s netstatus.Status) {
    if s.Available {
        fmt.Printf("Internet available (kind: %s)\n", s.Kind)
    } else {
        fmt.Printf("Internet unavailable\n")
    }
})
```
