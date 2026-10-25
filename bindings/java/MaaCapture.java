package maa.capture;

import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.Structure;

import java.util.ArrayList;
import java.util.List;

/**
 * maa-capture —— Java 绑定（JNA，无需 JNI 编译）
 *
 * 对应 MaaFramework 的 maa-framework-java 角色；封装两个外部扩展接口：
 *   1. 外部回调实现截图（对应 MaaCustomControllerCallbacks::screencap）
 *   2. Manager：内置 6 种 Win32 截图方式测速选择 + 外部单元注册
 *
 * 依赖：net.java.dev.jna:jna，由 fetch-jna.ps1 从 Maven Central 解析最新稳定版。
 * 加载：把 maa-capture.dll放入 JVM 的
 *       jna.library.path / PATH，或 java -Djna.library.path=build/Release。
 *
 * 用法：
 *   try (ScreencapManager mgr = new ScreencapManager(Pointer.NULL, Methods.NONE)) {
 *       mgr.registerCustomUnit("java-gradient", ctx, new Callbacks() {
 *           @Override public byte screencap(Pointer arg, ImageBuffer out) { ... return 1; }
 *       });
 *       mgr.connect();
 *       ImageBuffer img = mgr.screencap();
 *       byte[] bgr = img.copyBytes();
 *   }
 */
public final class MaaCapture
{
    /** 截图方式位掩码（与 C 头文件常数一致） */
    public static final class Methods
    {
        public static final long NONE = 0L;
        public static final long GDI = 1L << 0;
        public static final long FRAMEPOOL = 1L << 1;
        public static final long DXGI_DESKTOP_DUP = 1L << 2;
        public static final long DXGI_DESKTOP_DUP_WINDOW = 1L << 3;
        public static final long PRINTWINDOW = 1L << 4;
        public static final long SCREENDC = 1L << 5;
        public static final long ALL = -1L;
        public static final long FOREGROUND = DXGI_DESKTOP_DUP_WINDOW | SCREENDC;
        public static final long BACKGROUND = FRAMEPOOL | PRINTWINDOW;
    }

    // ---- 原生接口（C decl 约定） ----

    public interface NativeLib extends Library
    {
        NativeLib INSTANCE = Native.load("maa-capture", NativeLib.class);

        Pointer MaaWSManagerCreate(Pointer hwnd, long methods);
        void MaaWSManagerDestroy(Pointer handle);
        byte MaaWSManagerRegisterCustomUnit(Pointer handle, String name, Pointer callbacks, Pointer transArg);
        byte MaaWSManagerConnect(Pointer handle);
        byte MaaWSManagerScreencap(Pointer handle, ImageBuffer outBuffer);
        Pointer MaaWSManagerSelectedUnitName(Pointer handle);
        void MaaWSManagerInactive(Pointer handle);

        Pointer MaaWSCustomScreencapCreate(Pointer callbacks, Pointer transArg);
        void MaaWSCustomScreencapDestroy(Pointer handle);
        byte MaaWSCustomScreencapScreencap(Pointer handle, ImageBuffer outBuffer);
    }

    /** 输出图像缓冲（MaaWSImageBuffer） */
    public static class ImageBuffer extends Structure
    {
        public Pointer data; // 像素（BGR，归一化输出）
        public int width;
        public int height;
        public int channels;
        public int step;

        public ImageBuffer()
        {
        }

        @Override
        protected List<String> getFieldOrder()
        {
            return List.of("data", "width", "height", "channels", "step");
        }

        /** 拷贝像素为托管 byte[]（行主序） */
        public byte[] copyBytes()
        {
            if (data == null || width <= 0 || height <= 0) return new byte[0];
            int row = step > 0 ? step : width * channels;
            byte[] out = new byte[row * height];
            data.read(0, out, 0, out.length);
            return out;
        }
    }

    // ---- 外部截图回调（对应 MaaCustomControllerCallbacks.screencap） ----

    /** connect/connected/inactive 回调 */
    public interface BoolCallback extends Callback
    {
        byte invoke(Pointer transArg);
    }

    /** 截图回调：把图像写入 out（BGRA 或 BGR），成功返回 1 */
    public interface ScreencapCallback extends Callback
    {
        byte invoke(Pointer transArg, ImageBuffer out);
    }

    /** 外部回调集合（MaaWSCustomScreencapCallbacks） */
    public static class Callbacks extends Structure
    {
        public BoolCallback connect;
        public BoolCallback connected;
        public ScreencapCallback screencap;
        public BoolCallback inactive;

        @Override
        protected List<String> getFieldOrder()
        {
            return List.of("connect", "connected", "screencap", "inactive");
        }
    }

    // ---- Manager ----

    /** 截图管理器（MaaWSManager*） */
    public static class ScreencapManager implements AutoCloseable
    {
        private final Pointer handle;
        private final List<Callbacks> callbacksHold = new ArrayList<>(); // 防 GC

        public ScreencapManager(Pointer hwnd, long methods)
        {
            handle = NativeLib.INSTANCE.MaaWSManagerCreate(hwnd, methods);
            if (handle == null) throw new IllegalStateException("MaaWSManagerCreate failed");
        }

        /** 注册外部自定义截图单元（回调必须实现 screencap） */
        public boolean registerCustomUnit(String name, Callbacks callbacks)
        {
            if (callbacks.screencap == null)
                throw new IllegalArgumentException("screencap callback is required");
            callbacks.write(); // 确保回填到原生内存
            callbacksHold.add(callbacks); // JNA 内部为 Callback 生成 trampoline，保持引用防 GC
            byte ok = NativeLib.INSTANCE.MaaWSManagerRegisterCustomUnit(
                handle, name, callbacks.getPointer(), Pointer.NULL);
            return ok != 0;
        }

        public boolean connect()
        {
            return NativeLib.INSTANCE.MaaWSManagerConnect(handle) != 0;
        }

        /** 截图一次；成功返回图像信息（copyBytes 拷贝像素后使用） */
        public ImageBuffer screencap()
        {
            ImageBuffer out = new ImageBuffer();
            return NativeLib.INSTANCE.MaaWSManagerScreencap(handle, out) != 0 ? out : null;
        }

        public String selectedUnitName()
        {
            Pointer p = NativeLib.INSTANCE.MaaWSManagerSelectedUnitName(handle);
            return p == null ? null : p.getString(0);
        }

        public void inactive()
        {
            NativeLib.INSTANCE.MaaWSManagerInactive(handle);
        }

        @Override
        public void close()
        {
            NativeLib.INSTANCE.MaaWSManagerDestroy(handle);
        }
    }

    private MaaCapture()
    {
    }
}