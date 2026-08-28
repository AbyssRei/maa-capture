// maa-capture C# 绑定自测
// 运行：dotnet run --project test

using System;
using MaaCapture;

class Program
{
    const int W = 8;
    const int H = 6;
    static byte[] s_pixels;

    // ---- 外部截图回调：写入 BGRA 渐变 ----
    static byte OnScreencap(IntPtr transArg, ref ImageBuffer buffer)
    {
        buffer.Data = Marshal_AllocPixels();
        buffer.Width = W;
        buffer.Height = H;
        buffer.Channels = 4; // BGRA，桥接层转 BGR
        buffer.Step = W * 4;
        return 1;
    }

    static byte OnInactive(IntPtr transArg)
    {
        Console.WriteLine("[csharp] callbacks.inactive invoked");
        return 1;
    }

    // 用固定缓冲区持有像素，防 GC 移动
    static System.Runtime.InteropServices.GCHandle s_handle;

    static IntPtr Marshal_AllocPixels()
    {
        if (s_pixels == null)
        {
            s_pixels = new byte[W * H * 4];
            int i = 0;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    s_pixels[i++] = (byte)(x * 255 / W); // B
                    s_pixels[i++] = (byte)(y * 255 / H); // G
                    s_pixels[i++] = 128;                  // R
                    s_pixels[i++] = 255;                  // A
                }
            s_handle = System.Runtime.InteropServices.GCHandle.Alloc(s_pixels, System.Runtime.InteropServices.GCHandleType.Pinned);
        }
        return s_handle.AddrOfPinnedObject();
    }

    static int Main()
    {
        // 1) 外部回调单元注册进 Manager（无窗口依赖）
        var mgr = new ScreencapManager(IntPtr.Zero, ScreencapMethods.None);
        var cb = new ScreencapCallbacks { Screencap = OnScreencap, Inactive = OnInactive };
        if (!mgr.RegisterCustomUnit("csharp-gradient", cb))
        {
            Console.Error.WriteLine("register failed");
            return 1;
        }
        if (!mgr.Connect())
        {
            Console.Error.WriteLine("connect failed");
            return 1;
        }
        string? name = mgr.SelectedUnitName();
        if (name != "csharp-gradient")
        {
            Console.Error.WriteLine($"unexpected selected: {name}");
            return 1;
        }
        Console.WriteLine($"[csharp] selected unit: {name}");

        var img = mgr.Screencap();
        if (img == null)
        {
            Console.Error.WriteLine("screencap failed");
            return 1;
        }
        var pixels = img.Value.CopyBytes();
        if (img.Value.Width != W || img.Value.Height != H || img.Value.Channels != 3)
        {
            Console.Error.WriteLine($"unexpected image: {img.Value.Width}x{img.Value.Height} ch={img.Value.Channels}");
            return 1;
        }
        if (pixels[0] != 0 || pixels[1] != 0 || pixels[2] != 128)
        {
            Console.Error.WriteLine($"unexpected first pixel: {pixels[0]},{pixels[1]},{pixels[2]}");
            return 1;
        }
        Console.WriteLine($"[csharp] custom callback OK: {img.Value.Width}x{img.Value.Height} ch={img.Value.Channels}, first RGB={pixels[0]},{pixels[1]},{pixels[2]}");

        mgr.Inactive();
        mgr.Dispose();

        // 2) 内置方式（取当前前台窗口；无窗口时跳过）
        IntPtr hwnd = GetForegroundWindow();
        if (hwnd == IntPtr.Zero)
        {
            Console.WriteLine("[csharp] SKIP builtin (no foreground window)");
            Console.WriteLine("[csharp] ALL PASS");
            return 0;
        }
        var m2 = new ScreencapManager(hwnd, ScreencapMethods.All);
        if (!m2.Connect())
        {
            Console.WriteLine("[csharp] SKIP builtin (no method works)");
            Console.WriteLine("[csharp] ALL PASS");
            return 0;
        }
        var img2 = m2.Screencap();
        if (img2 == null)
        {
            Console.Error.WriteLine("builtin screencap failed");
            return 1;
        }
        Console.WriteLine($"[csharp] builtin OK: selected={m2.SelectedUnitName()} {img2.Value.Width}x{img2.Value.Height} ch={img2.Value.Channels}");
        m2.Dispose();

        Console.WriteLine("[csharp] ALL PASS");
        return 0;
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();
}