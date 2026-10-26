//go:build windows && amd64

package main

import (
	"fmt"
	capture "maa_capture"
	"os"
	"strings"
)

func main() {
	if path := os.Getenv("MAA_WS_LIB"); path != "" {
		capture.SetLibPath(path)
	}
	target, e := capture.CustomTarget("test", 8)
	if e != nil {
		panic(e)
	}
	manager, e := capture.NewManager(target, 0)
	if e != nil {
		panic(e)
	}
	defer manager.Close()
	if invalid, err := capture.NewManagerWithPolicy(target, 0, true); err == nil {
		invalid.Close()
		panic("policy silently accepted")
	}
	stopped := 0
	e = manager.RegisterCustomUnit("test", func() *capture.CaptureFrame {
		f, e := capture.ProviderFrame("test", []byte{10, 20, 30, 255}, 1, 1, 4, 4, 8)
		if e != nil {
			panic(e)
		}
		return &f
	}, func() { stopped++ })
	if e != nil {
		panic(e)
	}
	if !manager.Connect() {
		panic(manager.LastError())
	}
	capture.SetLibPath("missing-for-future-managers.dll") // Existing handles retain their own native API.
	f, e := manager.Screencap()
	if e != nil || f.Data[0] != 10 || f.Channels != 3 {
		panic(fmt.Sprintf("invalid frame: %+v, error=%v, native=%+v", f, e, manager.LastError()))
	}
	if f.Sample.Sample_id != 0 || f.Sample.Sampled_at_ns != 0 {
		panic("invented provider sampling")
	}
	manager.Inactive()
	manager.Inactive()
	if stopped != 1 {
		panic("cleanup")
	}
	manager.Close()
	manager.Close()
	nativePath := os.Getenv("MAA_WS_LIB")
	if nativePath == "" {
		nativePath = "maa-capture.dll"
	}
	capture.SetLibPath(nativePath)
	advanced, err := capture.NewManagerAdvanced(target, 0, capture.CapturePolicy{ConnectTimeoutNs: 1000000000})
	if err != nil && !strings.Contains(err.Error(), "does not support advanced") {
		panic(err)
	}
	if advanced != nil {
		defer advanced.Close()
		signal, err := advanced.CancellationHandle()
		if err != nil {
			panic(err)
		}
		defer signal.Close()
		if signal.Request() {
			panic("idle cancel")
		}
		err = advanced.RegisterCustomUnit("test", func() *capture.CaptureFrame {
			if !signal.Request() {
				panic("cancel request")
			}
			f, _ := capture.ProviderFrame("test", []byte{1, 2, 3, 255}, 1, 1, 4, 4, 8)
			return &f
		}, nil)
		connected := false
		if err == nil {
			connected = advanced.Connect()
		}
		if err != nil || connected || advanced.LastError().Code != 15 {
			panic(fmt.Sprintf("cancel: registration=%v connected=%v error=%+v", err, connected, advanced.LastError()))
		}
		advanced.Close()
		if signal.Request() {
			panic("retained cancel")
		}
		behavior, err := capture.MethodBehavior(16)
		if err != nil || behavior.Flags&1 == 0 {
			panic("behavior")
		}
	}
	fmt.Println("Go cgo ABI 2 OK")
}
