//go:build cgo

package maa_capture

// ============================================================================
// Go 外部回调截图接口 —— cgo 路径（对应 MaaCustomControllerCallbacks::screencap）
//
// 需要 CGO_ENABLED=1（Windows 上需 MinGW gcc 或 clang + MSVC 头）。
// 机制：
//   - C 前导段声明 MaaWSCustomScreencapCallbacks 与静态桥接函数，
//     桥接函数转发到 //export 的 Go 函数（trans_arg = 注册表 index 的地址）
//   - Go 闭包放入全局注册表保活（C 侧保存函数指针并在调用返回后使用）
//   - Destroy 时由 Manager 释放注册表项（ReleaseCustomUnits）
// ===========================================================================

/*
#include <stdint.h>
#include <stdlib.h>

typedef unsigned char MaaWSBool;
typedef struct MaaWSImageBuffer_
{
    unsigned char* data;
    int32_t width;
    int32_t height;
    int32_t channels;
    int32_t step;
} MaaWSImageBuffer;

typedef struct MaaWSCustomScreencapCallbacks_
{
    MaaWSBool (*connect)(void* trans_arg);
    MaaWSBool (*connected)(void* trans_arg);
    MaaWSBool (*screencap)(void* trans_arg, MaaWSImageBuffer* buffer);
    MaaWSBool (*inactive)(void* trans_arg);
} MaaWSCustomScreencapCallbacks;

// Go 侧 //export 实现的桥接函数（cgo 生成）
MaaWSBool mwsGoConnect(void* arg);
MaaWSBool mwsGoConnected(void* arg);
MaaWSBool mwsGoScreencap(void* arg, MaaWSImageBuffer* buffer);
MaaWSBool mwsGoInactive(void* arg);

// 用桥接函数填充回调结构体（trans_arg 原样透传）
static void MWS_FillCallbacks(MaaWSCustomScreencapCallbacks* cb)
{
    cb->connect = mwsGoConnect;
    cb->connected = mwsGoConnected;
    cb->screencap = mwsGoScreencap;
    cb->inactive = mwsGoInactive;
}
*/
import "C"

import (
	"errors"
	"fmt"
	"sync"
	"unsafe"
)

// ---- 注册表：Go 闭包保活 ----

var (
	registryMu sync.Mutex
	registry   = map[unsafe.Pointer]*ScreencapCallbacks{}
	allocated  []unsafe.Pointer // 由本包 malloc 的 C 结构体（Release 时统一释放）
)

func storeCallbacks(cb *ScreencapCallbacks) unsafe.Pointer {
	registryMu.Lock()
	defer registryMu.Unlock()
	// 用回调地址本身作为稳定 key（cb 由调用方持有引用）
	key := unsafe.Pointer(cb)
	registry[key] = cb
	return key
}

func lookupCallbacks(key unsafe.Pointer) *ScreencapCallbacks {
	registryMu.Lock()
	defer registryMu.Unlock()
	return registry[key]
}

func removeCallbacks(key unsafe.Pointer) {
	registryMu.Lock()
	defer registryMu.Unlock()
	delete(registry, key)
}

// ---- //export 桥接到 Go ----

//export mwsGoConnect
func mwsGoConnect(arg unsafe.Pointer) C.MaaWSBool {
	cb := lookupCallbacks(arg)
	if cb == nil || cb.Connect == nil {
		return 0
	}
	if cb.Connect(cb.Arg) {
		return 1
	}
	return 0
}

//export mwsGoConnected
func mwsGoConnected(arg unsafe.Pointer) C.MaaWSBool {
	cb := lookupCallbacks(arg)
	if cb == nil || cb.Connected == nil {
		return 0
	}
	if cb.Connected(cb.Arg) {
		return 1
	}
	return 0
}

//export mwsGoScreencap
func mwsGoScreencap(arg unsafe.Pointer, img *C.MaaWSImageBuffer) C.MaaWSBool {
	cb := lookupCallbacks(arg)
	if cb == nil || cb.Screencap == nil || img == nil {
		return 0
	}
	// 用 Go 视图包装 C 结构体（回调修改视图字段，返回后写回 C 结构体）
	view := &CImage{
		Data:     (*byte)(unsafe.Pointer(img.data)),
		Width:    int32(img.width),
		Height:   int32(img.height),
		Channels: int32(img.channels),
		Step:     int32(img.step),
	}
	if !cb.Screencap(cb.Arg, view) {
		return 0
	}
	// 写回 C 结构体（视图是副本，直接赋值不会作用到 C 内存）
	img.data = (*C.uchar)(unsafe.Pointer(view.Data))
	img.width = C.int32_t(view.Width)
	img.height = C.int32_t(view.Height)
	img.channels = C.int32_t(view.Channels)
	img.step = C.int32_t(view.Step)
	return 1
}

//export mwsGoInactive
func mwsGoInactive(arg unsafe.Pointer) C.MaaWSBool {
	cb := lookupCallbacks(arg)
	if cb == nil || cb.Inactive == nil {
		return 0
	}
	if cb.Inactive(cb.Arg) {
		return 1
	}
	return 0
}

// ---- 注册实现（替换基础文件的 stub var）----

func init() {
	registerCustomUnit = registerCustomUnitCgo
}

func registerCustomUnitCgo(handle uintptr, name string, callbacks ScreencapCallbacks) error {
	if callbacks.Screencap == nil {
		return errors.New("screencap callback is required")
	}

	// 注册表保活（key 指向 callbacks 本体）
	key := storeCallbacks(&callbacks)

	// 持久化 C 结构体（C 侧会保存指针，生命周期到 ReleaseCustomUnits）
	cStruct := C.malloc(C.size_t(unsafe.Sizeof(C.MaaWSCustomScreencapCallbacks{})))
	if cStruct == nil {
		removeCallbacks(key)
		return errors.New("malloc failed")
	}
	registryMu.Lock()
	allocated = append(allocated, cStruct)
	registryMu.Unlock()
	C.MWS_FillCallbacks((*C.MaaWSCustomScreencapCallbacks)(cStruct))

	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	r, _, _ := procManagerRegisterCustom.Call(
		handle,
		uintptr(unsafe.Pointer(cName)),
		uintptr(unsafe.Pointer(cStruct)),
		uintptr(key),
	)
	if r == 0 {
		C.free(unsafe.Pointer(cStruct))
		removeCallbacks(key)
		return fmt.Errorf("MaaWSManagerRegisterCustomUnit failed")
	}

	return nil
}

// ReleaseCustomUnits 释放本包注册的全部自定义单元（程序退出前调用）
func ReleaseCustomUnits() {
	registryMu.Lock()
	defer registryMu.Unlock()
	for _, p := range allocated {
		C.free(p)
	}
	allocated = nil
	registry = map[unsafe.Pointer]*ScreencapCallbacks{}
}