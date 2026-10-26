//go:build windows && amd64

// Package maa_capture provides the ABI 2 Windows x64 binding.
package maa_capture

import (
	"errors"
	"fmt"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"unicode/utf8"
	"unsafe"
)

var libraryPath = "maa-capture.dll"
var configMutex sync.RWMutex

func SetLibPath(path string) { configMutex.Lock(); libraryPath = path; configMutex.Unlock() }
func load() (map[string]*syscall.LazyProc, error) {
	configMutex.RLock()
	path := libraryPath
	configMutex.RUnlock()
	library := syscall.NewLazyDLL(path)
	procedures := map[string]*syscall.LazyProc{}
	abi := library.NewProc("MaaWSGetAbiVersion")
	if e := abi.Find(); e != nil {
		return nil, fmt.Errorf("maa-capture binding native unknown: ABI query: %w", e)
	}
	value, _, _ := abi.Call()
	if value != 2 {
		return nil, fmt.Errorf("expected ABI 2, got %d", value)
	}
	for _, name := range []string{"MaaWSManagerCreate", "MaaWSManagerDestroy", "MaaWSManagerConnect", "MaaWSManagerScreencap", "MaaWSManagerRegisterCustomUnit", "MaaWSManagerGetLastError", "MaaWSManagerInactive", "MaaWSFrameGetImage", "MaaWSFrameGetInfo", "MaaWSFrameDestroy"} {
		p := library.NewProc(name)
		if e := p.Find(); e != nil {
			return nil, e
		}
		procedures[name] = p
	}
	return procedures, nil
}

//go:uintptrescapes
func (m *Manager) call(name string, args ...uintptr) uintptr {
	r, _, _ := m.api[name].Call(args...)
	return r
}
func Text(out []byte, value string) error {
	if len(value) >= len(out) || strings.ContainsRune(value, 0) || !utf8.ValidString(value) {
		return errors.New("invalid UTF-8 key length or NUL")
	}
	clear(out)
	copy(out, value)
	return nil
}
func CustomTarget(key string, scope uint32) (MaaWSCaptureTarget, error) {
	t := MaaWSCaptureTarget{Struct_size: uint32(unsafe.Sizeof(MaaWSCaptureTarget{})), Type: 5, Scope: scope}
	e := Text(t.Source_key[:], key)
	return t, e
}
func MonitorTarget(key string) (MaaWSCaptureTarget, error) {
	t := MaaWSCaptureTarget{Struct_size: uint32(unsafe.Sizeof(MaaWSCaptureTarget{})), Type: 1}
	e := Text(t.Source_key[:], key)
	return t, e
}
func WindowTarget(id uint64) (MaaWSCaptureTarget, error) {
	t := MaaWSCaptureTarget{Struct_size: uint32(unsafe.Sizeof(MaaWSCaptureTarget{})), Type: 2, Platform: 1, Window_id: id}
	if id == 0 {
		return t, errors.New("window ID is required")
	}
	return t, nil
}

type CaptureFrame struct {
	Data                    []byte
	Width, Height, Channels uint32
	Step                    uint64
	Source                  MaaWSFrameInfo
}

func ProviderFrame(key string, data []byte, width, height, channels uint32, step uint64, scope uint32) (CaptureFrame, error) {
	source := MaaWSFrameInfo{Struct_size: uint32(unsafe.Sizeof(MaaWSFrameInfo{})), Target_type: 5, Scope: scope}
	e := Text(source.Source_key[:], key)
	return CaptureFrame{Data: data, Width: width, Height: height, Channels: channels, Step: step, Source: source}, e
}

type Manager struct {
	api     map[string]*syscall.LazyProc
	handle  uintptr
	scope   uint32
	release []func()
}

func NewManager(target MaaWSCaptureTarget, methods uint64) (*Manager, error) {
	api, err := load()
	if err != nil {
		return nil, err
	}
	m := &Manager{api: api, scope: target.Scope}
	options := MaaWSCaptureOptions{Struct_size: uint32(unsafe.Sizeof(MaaWSCaptureOptions{})), Target: target, Methods: methods}
	e := MaaWSError{Struct_size: uint32(unsafe.Sizeof(MaaWSError{}))}
	h := m.call("MaaWSManagerCreate", uintptr(unsafe.Pointer(&options)), uintptr(unsafe.Pointer(&e)))
	runtime.KeepAlive(options)
	if h == 0 {
		return nil, fmt.Errorf("create: %s", strings.TrimRight(string(e.Message[:]), "\x00"))
	}
	m.handle = h
	return m, nil
}
func (m *Manager) require() uintptr {
	if m.handle == 0 {
		panic("Manager is closed")
	}
	return m.handle
}
func (m *Manager) Connect() bool { return m.call("MaaWSManagerConnect", m.require()) != 0 }
func (m *Manager) LastError() MaaWSError {
	e := MaaWSError{Struct_size: uint32(unsafe.Sizeof(MaaWSError{}))}
	m.call("MaaWSManagerGetLastError", m.require(), uintptr(unsafe.Pointer(&e)))
	return e
}
func (m *Manager) Screencap() (*CaptureFrame, error) {
	var f uintptr
	if m.call("MaaWSManagerScreencap", m.require(), uintptr(unsafe.Pointer(&f))) == 0 {
		return nil, errors.New("capture failed; inspect LastError")
	}
	defer m.call("MaaWSFrameDestroy", f)
	i := MaaWSImageView{Struct_size: uint32(unsafe.Sizeof(MaaWSImageView{}))}
	info := MaaWSFrameInfo{Struct_size: uint32(unsafe.Sizeof(MaaWSFrameInfo{}))}
	if m.call("MaaWSFrameGetImage", f, uintptr(unsafe.Pointer(&i))) == 0 || m.call("MaaWSFrameGetInfo", f, uintptr(unsafe.Pointer(&info))) == 0 {
		return nil, errors.New("invalid frame")
	}
	if i.Data_size > uint64(^uint(0)>>1) {
		return nil, errors.New("frame exceeds Go address space")
	}
	pixels := append([]byte(nil), unsafe.Slice((*byte)(unsafe.Pointer(i.Data)), int(i.Data_size))...)
	return &CaptureFrame{pixels, i.Width, i.Height, i.Channels, i.Step, info}, nil
}
func (m *Manager) Inactive() { m.call("MaaWSManagerInactive", m.require()) }
func (m *Manager) Close() {
	if m.handle != 0 {
		m.call("MaaWSManagerDestroy", m.handle)
		m.handle = 0
		for _, f := range m.release {
			f()
		}
		m.release = nil
	}
}
