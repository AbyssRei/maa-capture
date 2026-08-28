// Package maa_capture 是 maa-capture 的 Go 绑定。
//
// 对应 MaaFramework 的 maa-framework-go 角色；封装截图管理器 C API。
// 纯 Go 实现（syscall.NewLazyDLL，零 cgo 依赖）：
//   - Manager：内置 6 种 Win32 截图方式（GDI/FramePool/DXGI_DD/DXGI_DD_Window/
//     PrintWindow/ScreenDC）的测速选择 + 截图
//
// 外部自定义截图单元（C 回调实现，对应 MaaCustomControllerCallbacks::screencap）
// 需要 cgo（Go 无法在无 cgo 条件下构造 C 函数指针），见 custom_cgo.go（//go:build cgo）。
//
// 用法（纯 Go）：
//   mgr := NewManager(hwnd, METHODS_ALL)
//   if !mgr.Connect() { ... }
//   shot, ok := mgr.Screencap()          // Screenshot{Buffer []byte BGR}
//   name := mgr.SelectedUnitName()
//   defer mgr.Destroy()
package maa_capture

import (
	"fmt"
	"syscall"
	"unsafe"
)

// 截图方式位掩码（与 C 头文件常数一致）
const (
	MethodNone              uint64 = 0
	MethodGDI               uint64 = 1 << 0
	MethodFramePool         uint64 = 1 << 1
	MethodDxgiDesktopDup    uint64 = 1 << 2
	MethodDxgiDesktopDupWin uint64 = 1 << 3
	MethodPrintWindow       uint64 = 1 << 4
	MethodScreenDC          uint64 = 1 << 5
	MethodAll               uint64 = ^uint64(0)
	MethodForeground               = MethodDxgiDesktopDupWin | MethodScreenDC
	MethodBackground               = MethodFramePool | MethodPrintWindow
)

// CImage 对应 C 的 MaaWSImageBuffer（Go 侧视图，外部回调写像素用）
type CImage struct {
	Data     *byte // 指向像素（BGRA 或 BGR）
	Width    int32
	Height   int32
	Channels int32
	Step     int32
}

// imageBuffer 对应 C 的 MaaWSImageBuffer（syscall 视图，对齐 8+4*4=24 字节）
type imageBuffer struct {
	data     uintptr
	width    int32
	height   int32
	channels int32
	step     int32
}

// ScreencapCallbacks 外部回调集合（对应 MaaWSCustomScreencapCallbacks）
type ScreencapCallbacks struct {
	// Screencap 必须实现：把图像写入 img（BGRA 或 BGR），成功返回 true
	Screencap func(arg any, img *CImage) bool
	Connect   func(arg any) bool
	Connected func(arg any) bool
	Inactive  func(arg any) bool
	// Arg 透传给回调的上下文
	Arg any
}

// registerCustomUnit 注册外部自定义截图单元。
// cgo 构建下在 custom_cgo.go 中实现；纯 Go 构建返回错误。
var registerCustomUnit = func(handle uintptr, name string, callbacks ScreencapCallbacks) error {
	return fmt.Errorf("custom callbacks require cgo build (CGO_ENABLED=1); use the manager builtin methods instead")
}

// 动态库过程（惰性解析：首次调用时按 PATH/搜索路径加载）
var (
	procManagerCreate         = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerCreate")
	procManagerDestroy        = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerDestroy")
	procManagerConnect        = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerConnect")
	procManagerScreencap      = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerScreencap")
	procManagerSelectedUnit   = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerSelectedUnitName")
	procManagerInactive       = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerInactive")
	procManagerRegisterCustom = syscall.NewLazyDLL("maa-capture.dll").NewProc("MaaWSManagerRegisterCustomUnit")
)

// Screenshot 截图结果（BGR 三通道，行主序）
type Screenshot struct {
	Width    int
	Height   int
	Channels int
	Step     int
	Buffer   []byte
}

// Manager 截图管理器
type Manager struct {
	handle uintptr
}

// NewManager 创建截图管理器。hwnd 为目标窗口句柄（0 = 无窗口，
// 内置前台方式将不可用）；methods 为内置方式位掩码。
func NewManager(hwnd uintptr, methods uint64) (*Manager, error) {
	r, _, _ := procManagerCreate.Call(hwnd, uintptr(methods))
	if r == 0 {
		return nil, fmt.Errorf("MaaWSManagerCreate failed")
	}
	return &Manager{handle: r}, nil
}

// RegisterCustomUnit 注册外部自定义截图单元（cgo 构建下有效，见 custom_cgo.go）
func (m *Manager) RegisterCustomUnit(name string, callbacks ScreencapCallbacks) error {
	return registerCustomUnit(m.handle, name, callbacks)
}

// Connect 连接并测速选择最快截图方式
func (m *Manager) Connect() bool {
	r, _, _ := procManagerConnect.Call(m.handle)
	return r != 0
}

// Screencap 截图一次（BGR），失败返回 ok=false
func (m *Manager) Screencap() (Screenshot, bool) {
	var out imageBuffer
	r, _, _ := procManagerScreencap.Call(m.handle, uintptr(unsafe.Pointer(&out)))
	if r == 0 {
		return Screenshot{}, false
	}
	row := out.step
	if row <= 0 {
		row = out.width * out.channels
	}
	total := int(row) * int(out.height)
	buf := make([]byte, total)
	if out.data != 0 {
		src := unsafe.Slice((*byte)(unsafe.Pointer(out.data)), total)
		copy(buf, src)
	}
	return Screenshot{
		Width:    int(out.width),
		Height:   int(out.height),
		Channels: int(out.channels),
		Step:     int(out.step),
		Buffer:   buf,
	}, true
}

// SelectedUnitName 当前选中的截图单元名
func (m *Manager) SelectedUnitName() string {
	r, _, _ := procManagerSelectedUnit.Call(m.handle)
	return cstr(r)
}

// Inactive 释放资源 / 恢复窗口状态
func (m *Manager) Inactive() {
	procManagerInactive.Call(m.handle)
}

// Destroy 销毁管理器
func (m *Manager) Destroy() {
	if m.handle != 0 {
		procManagerDestroy.Call(m.handle)
		m.handle = 0
	}
}

// cstr 读取 C 字符串（到 NUL 为止）
func cstr(p uintptr) string {
	if p == 0 {
		return ""
	}
	var b []byte
	for i := 0; ; i++ {
		c := *(*byte)(unsafe.Pointer(p + uintptr(i)))
		if c == 0 {
			break
		}
		b = append(b, c)
	}
	return string(b)
}

// ForegroundWindow 取当前前台窗口句柄（user32），无窗口返回 0
func ForegroundWindow() uintptr {
	user32 := syscall.NewLazyDLL("user32.dll")
	r, _, _ := user32.NewProc("GetForegroundWindow").Call()
	return r
}