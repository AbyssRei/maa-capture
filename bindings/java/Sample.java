package maa.capture;

import com.sun.jna.Memory;
import com.sun.jna.Pointer;

/**
 * maa-capture Java 绑定示例。
 *
 * 运行（需要 jna.jar 与 DLL 在 jna.library.path）：
 *   pwsh ./fetch-jna.ps1
 *   javac -d build-classes -cp .deps/jna.jar MaaCapture.java Sample.java
 *   java -Djna.library.path=build/Release -cp "build-classes;.deps/jna.jar" maa.capture.Sample
 */
public class Sample
{
    static final int W = 8;
    static final int H = 6;

    // 外部截图数据（JNA Memory 持有原生内存，回调期间保持稳定）
    static final Memory PIXELS = makeGradientMemory();

    static Memory makeGradientMemory()
    {
        byte[] px = new byte[W * H * 4];
        int i = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                px[i++] = (byte) (x * 255 / W); // B
                px[i++] = (byte) (y * 255 / H); // G
                px[i++] = (byte) 128;           // R
                px[i++] = (byte) 255;           // A
            }
        }
        Memory m = new Memory(px.length);
        m.write(0, px, 0, px.length);
        return m;
    }

    public static void main(String[] args)
    {
        // 1) 外部回调单元（输出结构的 data 指向 PIXELS 原生内存）
        try (MaaCapture.ScreencapManager mgr =
                 new MaaCapture.ScreencapManager(Pointer.NULL, MaaCapture.Methods.NONE)) {

            MaaCapture.Callbacks cb = new MaaCapture.Callbacks();
            cb.screencap = (transArg, out) -> {
                out.data = PIXELS; // Pointer -> 原生像素
                out.width = W;
                out.height = H;
                out.channels = 4; // BGRA -> 桥接层转 BGR
                out.step = W * 4;
                out.write();
                return (byte) 1;
            };

            if (!mgr.registerCustomUnit("java-gradient", cb)) {
                System.err.println("register failed");
                System.exit(1);
            }
            if (!mgr.connect()) {
                System.err.println("connect failed");
                System.exit(1);
            }
            System.out.println("[java] selected: " + mgr.selectedUnitName());

            MaaCapture.ImageBuffer img = mgr.screencap();
            if (img == null) {
                System.err.println("screencap failed");
                System.exit(1);
            }
            byte[] bgr = img.copyBytes();
            if (img.width != W || img.height != H || img.channels != 3) {
                System.err.printf("unexpected image: %dx%d ch=%d%n", img.width, img.height, img.channels);
                System.exit(1);
            }
            if ((bgr[0] & 0xFF) != 0 || (bgr[1] & 0xFF) != 0 || (bgr[2] & 0xFF) != 128) {
                System.err.printf("unexpected first pixel: %d,%d,%d%n", bgr[0] & 0xFF, bgr[1] & 0xFF, bgr[2] & 0xFF);
                System.exit(1);
            }
            System.out.printf("[java] custom callback OK: %dx%d ch=%d firstRGB=%d,%d,%d%n",
                img.width, img.height, img.channels, bgr[0] & 0xFF, bgr[1] & 0xFF, bgr[2] & 0xFF);
        }

        // 2) 内置方式（需要真实窗口句柄；无窗口跳过）
        long hwnd = getForegroundWindow();
        if (hwnd == 0) {
            System.out.println("[java] SKIP builtin (no foreground window)");
            System.out.println("ALL PASS");
            return;
        }
        try (MaaCapture.ScreencapManager m2 =
                 new MaaCapture.ScreencapManager(new Pointer(hwnd), MaaCapture.Methods.ALL)) {
            if (!m2.connect()) {
                System.out.println("[java] SKIP builtin (no method works on this window)");
                System.out.println("ALL PASS");
                return;
            }
            MaaCapture.ImageBuffer img = m2.screencap();
            if (img == null) {
                System.err.println("builtin screencap failed");
                System.exit(1);
            }
            System.out.printf("[java] builtin OK: selected=%s %dx%d ch=%d%n",
                m2.selectedUnitName(), img.width, img.height, img.channels);
        }

        System.out.println("ALL PASS");
    }

    /** user32!GetForegroundWindow（经 JNA 直接调用系统库） */
    interface User32 extends com.sun.jna.Library
    {
        User32 INSTANCE = com.sun.jna.Native.load("user32", User32.class);
        Pointer GetForegroundWindow();
    }

    static long getForegroundWindow()
    {
        try {
            Pointer p = User32.INSTANCE.GetForegroundWindow();
            return p == null ? 0 : Pointer.nativeValue(p);
        }
        catch (Throwable t) {
            return 0;
        }
    }
}