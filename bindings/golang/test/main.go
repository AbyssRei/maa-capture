// maa-capture Go 绑定自测
// 运行：go run ./test [dll目录]   （dll 目录会加入 PATH，缺省走系统 PATH）
package main

import (
	"fmt"
	"os"

	maa "maa_capture"
)

// 8x6 BGRA 渐变（Go 堆分配；Go GC 不移动对象，&pixels[0] 稳定可用）
const (
	imgW = 8
	imgH = 6
)

func makeGradient() []byte {
	buf := make([]byte, imgW*imgH*4)
	i := 0
	for y := 0; y < imgH; y++ {
		for x := 0; x < imgW; x++ {
			buf[i] = byte(x * 255 / imgW) // B
			buf[i+1] = byte(y * 255 / imgH) // G
			buf[i+2] = 128                  // R
			buf[i+3] = 255                  // A
			i += 4
		}
	}
	return buf
}

var pixels = makeGradient()

func testCustomCallback(mgr *maa.Manager) {
	cb := maa.ScreencapCallbacks{
		Screencap: func(arg any, img *maa.CImage) bool {
			// C 侧传入的 CImage.data 初始为 NULL，这里直接设置指针与尺寸
			img.Data = &pixels[0]
			img.Width = imgW
			img.Height = imgH
			img.Channels = 4 // BGRA，桥接层转 BGR
			img.Step = imgW * 4
			return true
		},
		Inactive: func(arg any) bool {
			fmt.Println("[go] callbacks.inactive invoked")
			return true
		},
	}

	err := mgr.RegisterCustomUnit("go-gradient", cb)
	if err != nil {
		fmt.Printf("[go] custom callback SKIP: %v\n", err)
		return
	}
	if !mgr.Connect() {
		fmt.Println("[go] custom callback connect failed")
		os.Exit(1)
	}
	fmt.Println("[go] selected unit:", mgr.SelectedUnitName())

	shot, ok := mgr.Screencap()
	if !ok {
		fmt.Println("[go] custom callback screencap failed")
		os.Exit(1)
	}
	if shot.Width != imgW || shot.Height != imgH || shot.Channels != 3 {
		fmt.Printf("[go] unexpected image: %dx%d ch=%d\n", shot.Width, shot.Height, shot.Channels)
		os.Exit(1)
	}
	if shot.Buffer[0] != 0 || shot.Buffer[1] != 0 || shot.Buffer[2] != 128 {
		fmt.Printf("[go] unexpected first pixel: %d,%d,%d\n", shot.Buffer[0], shot.Buffer[1], shot.Buffer[2])
		os.Exit(1)
	}
	fmt.Printf("[go] custom callback OK: %dx%d ch=%d firstRGB=%d,%d,%d\n",
		shot.Width, shot.Height, shot.Channels, shot.Buffer[0], shot.Buffer[1], shot.Buffer[2])
}

func testBuiltin() {
	hwnd := maa.ForegroundWindow()
	if hwnd == 0 {
		fmt.Println("[go] SKIP builtin (no foreground window)")
		return
	}
	mgr, err := maa.NewManager(hwnd, maa.MethodAll)
	if err != nil {
		fmt.Println("[go] manager create failed:", err)
		os.Exit(1)
	}
	if !mgr.Connect() {
		fmt.Println("[go] SKIP builtin (no method works on this window)")
		return
	}
	shot, ok := mgr.Screencap()
	if !ok {
		fmt.Println("[go] builtin screencap failed")
		os.Exit(1)
	}
	fmt.Printf("[go] builtin OK: selected=%s %dx%d ch=%d\n",
		mgr.SelectedUnitName(), shot.Width, shot.Height, shot.Channels)
	mgr.Inactive()
	mgr.Destroy()
}

func main() {
	// DLL 在首次过程调用时才解析，这里提前把目录加进 PATH 即可
	if len(os.Args) > 1 {
		os.Setenv("PATH", os.Args[1]+";"+os.Getenv("PATH"))
	}

	// 1) 外部回调单元（cgo 构建下有效；纯 Go 构建打印 SKIP）
	mgr, err := maa.NewManager(0, maa.MethodNone)
	if err != nil {
		fmt.Println("[go] manager create failed:", err)
		os.Exit(1)
	}
	testCustomCallback(mgr)
	maa.ReleaseCustomUnits()
	mgr.Inactive()
	mgr.Destroy()

	// 2) 内置方式（需要真实窗口；纯 syscall 路径也可用）
	testBuiltin()

	fmt.Println("[go] ALL PASS")
}