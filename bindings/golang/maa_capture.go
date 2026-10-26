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
	featuresProc := library.NewProc("MaaWSGetApiFeatures")
	if e := featuresProc.Find(); e != nil {
		return nil, e
	}
	features, _, _ := featuresProc.Call()
	for name, bit := range map[string]uintptr{"MaaWSFrameGetSampleInfo": 8, "MaaWSManagerCreateWithPolicy": 16, "MaaWSManagerCreateAdvanced": 32, "MaaWSGetMethodBehavior": 64, "MaaWSManagerGetCancellationHandle": 128, "MaaWSCancelRequest": 128, "MaaWSCancelHandleDestroy": 128} {
		if features&bit != 0 {
			p := library.NewProc(name)
			if e := p.Find(); e != nil {
				return nil, e
			}
			procedures[name] = p
		}
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
	Sample                  MaaWSFrameSampleInfo
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
type CapturePolicy struct {
	Flags, MaxSampleAgeNs, ConnectTimeoutNs, CaptureTimeoutNs, FirstFrameTimeoutNs uint64
}

const (
	PolicyFramePoolCache   uint64 = 1
	PolicyDxgiCache        uint64 = 2
	PolicyStrictBackground uint64 = 4
	PolicyFreshOnly        uint64 = 8
	PolicyDiagnostics      uint64 = 16
)

type Cancellation struct {
	api    map[string]*syscall.LazyProc
	handle uintptr
	mutex  sync.Mutex
}

func (c *Cancellation) Request() bool {
	c.mutex.Lock()
	defer c.mutex.Unlock()
	if c.handle == 0 {
		panic("Cancellation handle is closed")
	}
	r, _, _ := c.api["MaaWSCancelRequest"].Call(c.handle)
	return r&0xff != 0
}
func (c *Cancellation) Close() {
	c.mutex.Lock()
	defer c.mutex.Unlock()
	if c.handle != 0 {
		c.api["MaaWSCancelHandleDestroy"].Call(c.handle)
		c.handle = 0
	}
}
func (m *Manager) CancellationHandle() (*Cancellation, error) {
	if m.api["MaaWSManagerGetCancellationHandle"] == nil {
		return nil, errors.New("native library does not support cancellation")
	}
	h := m.call("MaaWSManagerGetCancellationHandle", m.require())
	if h == 0 {
		return nil, errors.New("cancellation handle creation failed")
	}
	return &Cancellation{api: m.api, handle: h}, nil
}
func MethodBehavior(method uint64) (MaaWSMethodBehavior, error) {
	api, e := load()
	b := MaaWSMethodBehavior{Struct_size: uint32(unsafe.Sizeof(MaaWSMethodBehavior{}))}
	if e != nil {
		return b, e
	}
	if api["MaaWSGetMethodBehavior"] == nil {
		return b, errors.New("native library does not support method behavior queries")
	}
	m := Manager{api: api}
	err := MaaWSError{Struct_size: uint32(unsafe.Sizeof(MaaWSError{}))}
	if m.call("MaaWSGetMethodBehavior", uintptr(method), uintptr(unsafe.Pointer(&b)), uintptr(unsafe.Pointer(&err)))&0xff == 0 {
		return b, errors.New(strings.TrimRight(string(err.Message[:]), "\x00"))
	}
	return b, nil
}

func NewManager(target MaaWSCaptureTarget, methods uint64) (*Manager, error) {
	return NewManagerWithPolicy(target, methods, false)
}
func NewManagerWithPolicy(target MaaWSCaptureTarget, methods uint64, framepoolCache bool) (*Manager, error) {
	return newManager(target, methods, framepoolCache, nil)
}
func NewManagerAdvanced(target MaaWSCaptureTarget, methods uint64, policy CapturePolicy) (*Manager, error) {
	return newManager(target, methods, false, &policy)
}
func newManager(target MaaWSCaptureTarget, methods uint64, framepoolCache bool, policy *CapturePolicy) (*Manager, error) {
	api, err := load()
	if err != nil {
		return nil, err
	}
	m := &Manager{api: api, scope: target.Scope}
	options := MaaWSCaptureOptions{Struct_size: uint32(unsafe.Sizeof(MaaWSCaptureOptions{})), Target: target, Methods: methods}
	e := MaaWSError{Struct_size: uint32(unsafe.Sizeof(MaaWSError{}))}
	var h uintptr
	if policy != nil {
		if api["MaaWSManagerCreateAdvanced"] == nil {
			return nil, errors.New("native library does not support advanced capture policies")
		}
		native := MaaWSAdvancedCapturePolicy{Struct_size: uint32(unsafe.Sizeof(MaaWSAdvancedCapturePolicy{})), Flags: policy.Flags, Max_sample_age_ns: policy.MaxSampleAgeNs, Connect_timeout_ns: policy.ConnectTimeoutNs, Capture_timeout_ns: policy.CaptureTimeoutNs, First_frame_timeout_ns: policy.FirstFrameTimeoutNs}
		if framepoolCache {
			native.Flags |= 1
		}
		h = m.call("MaaWSManagerCreateAdvanced", uintptr(unsafe.Pointer(&options)), uintptr(unsafe.Pointer(&native)), uintptr(unsafe.Pointer(&e)))
	} else if framepoolCache {
		if api["MaaWSManagerCreateWithPolicy"] == nil {
			return nil, errors.New("native library does not support FramePool caching")
		}
		policy := MaaWSCapturePolicy{Struct_size: uint32(unsafe.Sizeof(MaaWSCapturePolicy{})), Flags: 1}
		h = m.call("MaaWSManagerCreateWithPolicy", uintptr(unsafe.Pointer(&options)), uintptr(unsafe.Pointer(&policy)), uintptr(unsafe.Pointer(&e)))
	} else {
		h = m.call("MaaWSManagerCreate", uintptr(unsafe.Pointer(&options)), uintptr(unsafe.Pointer(&e)))
	}
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
func (m *Manager) Connect() bool { return m.call("MaaWSManagerConnect", m.require())&0xff != 0 }
func (m *Manager) LastError() MaaWSError {
	e := MaaWSError{Struct_size: uint32(unsafe.Sizeof(MaaWSError{}))}
	m.call("MaaWSManagerGetLastError", m.require(), uintptr(unsafe.Pointer(&e)))
	return e
}
func (m *Manager) Screencap() (*CaptureFrame, error) {
	var f uintptr
	if m.call("MaaWSManagerScreencap", m.require(), uintptr(unsafe.Pointer(&f)))&0xff == 0 {
		return nil, errors.New("capture failed; inspect LastError")
	}
	defer m.call("MaaWSFrameDestroy", f)
	i := MaaWSImageView{Struct_size: uint32(unsafe.Sizeof(MaaWSImageView{}))}
	info := MaaWSFrameInfo{Struct_size: uint32(unsafe.Sizeof(MaaWSFrameInfo{}))}
	if m.call("MaaWSFrameGetImage", f, uintptr(unsafe.Pointer(&i)))&0xff == 0 || m.call("MaaWSFrameGetInfo", f, uintptr(unsafe.Pointer(&info)))&0xff == 0 {
		return nil, errors.New("invalid frame")
	}
	if i.Data_size > uint64(^uint(0)>>1) {
		return nil, errors.New("frame exceeds Go address space")
	}
	pixels := append([]byte(nil), unsafe.Slice((*byte)(unsafe.Pointer(i.Data)), int(i.Data_size))...)
	sample := MaaWSFrameSampleInfo{Struct_size: uint32(unsafe.Sizeof(MaaWSFrameSampleInfo{}))}
	if m.api["MaaWSFrameGetSampleInfo"] != nil && m.call("MaaWSFrameGetSampleInfo", f, uintptr(unsafe.Pointer(&sample)))&0xff == 0 {
		return nil, errors.New("invalid sample metadata")
	}
	return &CaptureFrame{pixels, i.Width, i.Height, i.Channels, i.Step, info, sample}, nil
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
