//go:build windows && amd64 && cgo

package maa_capture

/*
#cgo CFLAGS: -I../../include
#include <stdlib.h>
#include <string.h>
#include "maa-capture/MaaWSCustomScreencap.h"
extern MaaWSBool mwsGoCapture(uintptr_t, MaaWSFrameInput*);
extern void mwsGoRelease(uintptr_t);
extern void mwsGoInactive(uintptr_t);
static MaaWSBool mws_capture(void* ctx, MaaWSFrameInput* input, MaaWSError* error) { return mwsGoCapture((uintptr_t)ctx,input); }
static void mws_release(void* ctx) { mwsGoRelease((uintptr_t)ctx); }
static void mws_inactive(void* ctx) { mwsGoInactive((uintptr_t)ctx); }
static MaaWSCustomScreencapCallbacks mws_callbacks(void) {
    MaaWSCustomScreencapCallbacks cb={0};cb.struct_size=sizeof(cb);cb.screencap=mws_capture;cb.release_frame=mws_release;cb.inactive=mws_inactive;return cb;
}
*/
import "C"
import (
	"fmt"
	"runtime"
	"runtime/cgo"
	"unsafe"
)

type callbackState struct {
	capture  func() *CaptureFrame
	inactive func()
	pixels   unsafe.Pointer
}

//export mwsGoCapture
func mwsGoCapture(token C.uintptr_t, input *C.MaaWSFrameInput) (result C.MaaWSBool) {
	defer func() {
		if recover() != nil {
			result = 0
		}
	}()
	state := cgo.Handle(token).Value().(*callbackState)
	f := state.capture()
	if f == nil {
		return 0
	}
	state.pixels = C.CBytes(f.Data)
	input.image.struct_size = C.uint32_t(C.sizeof_MaaWSImageView)
	input.image.width = C.uint32_t(f.Width)
	input.image.height = C.uint32_t(f.Height)
	input.image.channels = C.uint32_t(f.Channels)
	input.image.step = C.uint64_t(f.Step)
	input.image.data_size = C.uint64_t(len(f.Data))
	input.image.data = (*C.uint8_t)(state.pixels)
	C.memcpy(unsafe.Pointer(&input.info), unsafe.Pointer(&f.Source), C.size_t(unsafe.Sizeof(f.Source)))
	return 1
}

//export mwsGoRelease
func mwsGoRelease(token C.uintptr_t) {
	state := cgo.Handle(token).Value().(*callbackState)
	if state.pixels != nil {
		C.free(state.pixels)
		state.pixels = nil
	}
}

//export mwsGoInactive
func mwsGoInactive(token C.uintptr_t) {
	defer func() { recover() }()
	state := cgo.Handle(token).Value().(*callbackState)
	if state.inactive != nil {
		state.inactive()
	}
}
func (m *Manager) RegisterCustomUnit(key string, capture func() *CaptureFrame, inactive func()) error {
	if uintptr(C.sizeof_MaaWSFrameInfo) != unsafe.Sizeof(MaaWSFrameInfo{}) || uintptr(C.sizeof_MaaWSFrameInput) != unsafe.Sizeof(MaaWSFrameInput{}) {
		return fmt.Errorf("C/Go ABI structure size mismatch; rebuild cgo after changing the public header")
	}
	if capture == nil {
		return fmt.Errorf("capture callback is required")
	}
	bytes := make([]byte, 512)
	if e := Text(bytes, key); e != nil {
		return e
	}
	state := &callbackState{capture: capture, inactive: inactive}
	token := cgo.NewHandle(state)
	cb := C.mws_callbacks()
	ok := m.call("MaaWSManagerRegisterCustomUnit", m.require(), uintptr(unsafe.Pointer(&bytes[0])), uintptr(m.scope), 0, uintptr(unsafe.Pointer(&cb)), uintptr(token)) & 0xff
	runtime.KeepAlive(bytes)
	if ok == 0 {
		token.Delete()
		return fmt.Errorf("provider registration failed")
	}
	m.release = append(m.release, func() { mwsGoRelease(C.uintptr_t(token)); token.Delete() })
	return nil
}
