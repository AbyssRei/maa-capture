//go:build windows && amd64

package main

import (
	"fmt"
	capture "maa_capture"
	"os"
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
	manager.Inactive()
	manager.Inactive()
	if stopped != 1 {
		panic("cleanup")
	}
	manager.Close()
	manager.Close()
	fmt.Println("Go cgo ABI 2 OK")
}
