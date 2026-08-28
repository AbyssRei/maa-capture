// ============================================================================
// maa-capture —— C# 绑定（P/Invoke）
//
// 对应 MaaFramework.Binding.CSharp 的角色；封装两个外部扩展接口：
//   1. 外部回调实现截图（对应 MaaCustomControllerCallbacks::screencap）
//   2. Manager：内置 6 种 Win32 截图方式测速选择 + 外部单元注册
//
// 生命周期说明：
//   - 回调结构体被 C 侧持有（CustomScreencapBridge 保存指针并在后续调用），
//     因此必须用 AllocHGlobal 持久化原生结构体，并把委托保存在托管字段里防 GC；
//     Dispose 时释放。
//   - MaaWSManagerScreencap 返回的 ImageBuffer.Data 指向句柄内部图像，
//     有效期至下一次调用 / Dispose（即回调返回后即失效）。
//
// 用法：
//   using MaaCapture;
//   var mgr = new ScreencapManager(IntPtr.Zero, ScreencapMethods.None);
//   mgr.RegisterCustomUnit("my-unit", new ScreencapCallbacks { Screencap = OnScreencap });
//   mgr.Connect();
//   var img = mgr.Screencap();          // ImageBuffer（含像素数据）
//   byte[] bgr = img.CopyBytes();       // 拷贝到托管数组
//   Console.WriteLine(mgr.SelectedUnitName());
//   mgr.Dispose();
// ============================================================================

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace MaaCapture
{
    internal static class Native
    {
        public const string DllName = "maa-capture";
    }

    /// <summary>截图方式位掩码（与 C 头文件常数一致）。</summary>
    public static class ScreencapMethods
    {
        public const ulong None = 0;
        public const ulong GDI = 1UL << 0;
        public const ulong FramePool = 1UL << 1;
        public const ulong DxgiDesktopDup = 1UL << 2;
        public const ulong DxgiDesktopDupWindow = 1UL << 3;
        public const ulong PrintWindow = 1UL << 4;
        public const ulong ScreenDC = 1UL << 5;
        public const ulong All = ulong.MaxValue;
        public const ulong Foreground = DxgiDesktopDupWindow | ScreenDC;
        public const ulong Background = FramePool | PrintWindow;
    }

    /// <summary>输出图像缓冲（MaaWSImageBuffer）。</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ImageBuffer
    {
        public IntPtr Data; // 指向图像数据（BGR 三通道）
        public int Width;
        public int Height;
        public int Channels;
        public int Step; // 行字节数；<=0 表示连续

        /// <summary>把像素拷贝到托管数组（行主序，数据量 = Step*Height）。</summary>
        public byte[] CopyBytes()
        {
            if (Data == IntPtr.Zero || Width <= 0 || Height <= 0) return Array.Empty<byte>();
            int row = Step > 0 ? Step : Width * Channels;
            var buf = new byte[row * Height];
            Marshal.Copy(Data, buf, 0, buf.Length);
            return buf;
        }
    }

    /// <summary>外部截图回调（对应 MaaCustomControllerCallbacks.screencap）。</summary>
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate byte ScreencapCallback(IntPtr transArg, ref ImageBuffer buffer);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate byte BoolCallback(IntPtr transArg);

    /// <summary>外部回调集合（MaaWSCustomScreencapCallbacks）。</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ScreencapCallbacks
    {
        public BoolCallback Connect;
        public BoolCallback Connected;
        public ScreencapCallback Screencap;
        public BoolCallback Inactive;
    }

    /// <summary>原生回调结构体的托管持有者（持久化内存 + 委托保活）。</summary>
    internal sealed class NativeCallbacks : IDisposable
    {
        public readonly IntPtr Pointer;
        private readonly ScreencapCallbacks _managed; // 持有委托引用，防 GC

        public NativeCallbacks(ScreencapCallbacks callbacks)
        {
            if (callbacks.Screencap == null)
                throw new ArgumentException("screencap callback is required", nameof(callbacks));
            _managed = callbacks;
            Pointer = Marshal.AllocHGlobal(Marshal.SizeOf<ScreencapCallbacks>());
            Marshal.StructureToPtr(callbacks, Pointer, false);
        }

        public void Dispose()
        {
            if (Pointer != IntPtr.Zero)
            {
                Marshal.DestroyStructure<ScreencapCallbacks>(Pointer);
                Marshal.FreeHGlobal(Pointer);
            }
        }
    }

    /// <summary>截图管理器（MaaWSManager*）。</summary>
    public sealed class ScreencapManager : IDisposable
    {
        private const string DllName = Native.DllName;

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr MaaWSManagerCreate(IntPtr hwnd, ulong methods);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void MaaWSManagerDestroy(IntPtr handle);

        // 回调结构体以 IntPtr 传入（指向 NativeCallbacks 持久化内存）
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern byte MaaWSManagerRegisterCustomUnit(IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, IntPtr callbacks, IntPtr transArg);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern byte MaaWSManagerConnect(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern byte MaaWSManagerScreencap(IntPtr handle, ref ImageBuffer outBuffer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr MaaWSManagerSelectedUnitName(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void MaaWSManagerInactive(IntPtr handle);

        private IntPtr _handle;
        private readonly List<NativeCallbacks> _callbacks = new();
        private readonly object _lock = new();

        public ScreencapManager(IntPtr hwnd, ulong methods)
        {
            _handle = MaaWSManagerCreate(hwnd, methods);
            if (_handle == IntPtr.Zero)
                throw new InvalidOperationException("MaaWSManagerCreate failed");
        }

        /// <summary>注册外部自定义截图单元（C 回调实现）。</summary>
        public bool RegisterCustomUnit(string name, ScreencapCallbacks callbacks)
        {
            lock (_lock)
            {
                var native = new NativeCallbacks(callbacks);
                bool ok = MaaWSManagerRegisterCustomUnit(_handle, name, native.Pointer, IntPtr.Zero) != 0;
                if (ok)
                    _callbacks.Add(native);
                else
                    native.Dispose();
                return ok;
            }
        }

        public bool Connect()
        {
            lock (_lock) return MaaWSManagerConnect(_handle) != 0;
        }

        /// <summary>
        /// 截图一次，成功返回图像信息（Data 指向句柄内部图像，下一次调用前有效）。
        /// 需要持久数据请调用 CopyBytes()。
        /// </summary>
        public ImageBuffer? Screencap()
        {
            lock (_lock)
            {
                var buf = new ImageBuffer();
                return MaaWSManagerScreencap(_handle, ref buf) != 0 ? buf : (ImageBuffer?)null;
            }
        }

        public string? SelectedUnitName()
        {
            lock (_lock)
            {
                IntPtr p = MaaWSManagerSelectedUnitName(_handle);
                return p == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(p);
            }
        }

        public void Inactive()
        {
            lock (_lock) MaaWSManagerInactive(_handle);
        }

        public void Dispose()
        {
            lock (_lock)
            {
                if (_handle != IntPtr.Zero)
                {
                    MaaWSManagerDestroy(_handle);
                    _handle = IntPtr.Zero;
                }
                foreach (var cb in _callbacks) cb.Dispose();
                _callbacks.Clear();
            }
            GC.SuppressFinalize(this);
        }

        ~ScreencapManager() => Dispose();
    }

    /// <summary>纯回调句柄封装（MaaWSCustomScreencap*）。</summary>
    public sealed class CustomScreencapHandle : IDisposable
    {
        private const string DllName = Native.DllName;
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr MaaWSCustomScreencapCreate(IntPtr callbacks, IntPtr transArg);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void MaaWSCustomScreencapDestroy(IntPtr handle);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern byte MaaWSCustomScreencapScreencap(IntPtr handle, ref ImageBuffer outBuffer);

        private IntPtr _handle;
        private NativeCallbacks? _callbacks;

        public CustomScreencapHandle(ScreencapCallbacks callbacks)
        {
            _callbacks = new NativeCallbacks(callbacks);
            _handle = MaaWSCustomScreencapCreate(_callbacks.Pointer, IntPtr.Zero);
            if (_handle == IntPtr.Zero)
            {
                _callbacks.Dispose();
                _callbacks = null;
                throw new InvalidOperationException("MaaWSCustomScreencapCreate failed");
            }
        }

        public ImageBuffer? Screencap()
        {
            var buf = new ImageBuffer();
            return MaaWSCustomScreencapScreencap(_handle, ref buf) != 0 ? buf : (ImageBuffer?)null;
        }

        public void Dispose()
        {
            if (_handle != IntPtr.Zero)
            {
                MaaWSCustomScreencapDestroy(_handle);
                _handle = IntPtr.Zero;
            }
            _callbacks?.Dispose();
            _callbacks = null;
            GC.SuppressFinalize(this);
        }

        ~CustomScreencapHandle() => Dispose();
    }
}