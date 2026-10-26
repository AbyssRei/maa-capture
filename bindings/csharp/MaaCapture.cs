using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
namespace MaaCapture {
public static class Abi {
    public static T New<T>() where T:struct => (T)Create(typeof(T));
    private static object Create(Type type) {
        object obj = Activator.CreateInstance(type)!;
        foreach (var f in type.GetFields()) {
            var array = f.GetCustomAttribute<MarshalAsAttribute>();
            if (f.FieldType.IsArray && array != null) f.SetValue(obj,Array.CreateInstance(f.FieldType.GetElementType()!,array.SizeConst));
            else if (f.FieldType.IsValueType && !f.FieldType.IsPrimitive && !f.FieldType.IsEnum) f.SetValue(obj,Create(f.FieldType));
            else if (f.Name == "struct_size") f.SetValue(obj,(uint)Marshal.SizeOf(type));
        }
        return obj;
    }
    public static byte[] Text(string value,int capacity) {
        if (value.Contains('\0')) throw new ArgumentException("NUL in UTF-8 string");
        var encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity) throw new ArgumentOutOfRangeException(nameof(value));
        var output = new byte[capacity]; encoded.CopyTo(output,0); return output;
    }
    public static string Text(byte[] value) { int end = Array.IndexOf(value,(byte)0); return Encoding.UTF8.GetString(value,0,end<0?value.Length:end); }
}
public sealed class CaptureTarget {
    internal readonly MaaWSCaptureTarget Value;
    private CaptureTarget(MaaWSCaptureTarget value) { Value=value; }
    public static CaptureTarget Monitor(string key) { var t=Abi.New<MaaWSCaptureTarget>(); t.type=1; t.source_key=Abi.Text(key,512); return new(t); }
    public static CaptureTarget Window(ulong id,ulong processId=0) { if(id==0)throw new ArgumentException("Window ID is required"); var t=Abi.New<MaaWSCaptureTarget>(); t.type=2;t.platform=1;t.window_id=id;t.process_id=processId;return new(t); }
    public static CaptureTarget Custom(string key,uint scope=8) { var t=Abi.New<MaaWSCaptureTarget>();t.type=5;t.scope=scope;t.source_key=Abi.Text(key,512);return new(t); }
}
public sealed record CaptureFrame(byte[] Data,uint Width,uint Height,uint Channels,ulong Step,MaaWSFrameInfo Source) {
    public MaaWSFrameSampleInfo Sample {get;init;}
}
[Flags] public enum CapturePolicyFlags:ulong {FramePoolCache=1,DxgiCache=2,StrictBackground=4,FreshOnly=8,Diagnostics=16}
public sealed record CapturePolicy(CapturePolicyFlags Flags=0,ulong MaxSampleAgeNs=0,ulong ConnectTimeoutNs=0,ulong CaptureTimeoutNs=0,ulong FirstFrameTimeoutNs=0);
public sealed class Cancellation:IDisposable {
    private IntPtr handle;private readonly object gate=new();
    internal Cancellation(IntPtr value){handle=value;if(value==IntPtr.Zero)throw new InvalidOperationException("Cancellation handle creation failed");}
    public bool Request(){lock(gate){if(handle==IntPtr.Zero)throw new ObjectDisposedException(nameof(Cancellation));return Native.MaaWSCancelRequest(handle)!=0;}}
    public void Dispose(){lock(gate){if(handle!=IntPtr.Zero){Native.MaaWSCancelHandleDestroy(handle);handle=IntPtr.Zero;}}GC.SuppressFinalize(this);}
    ~Cancellation(){Dispose();}
}
internal static class Native {
    const string Lib="maa-capture";
    internal static void VerifySymbols() {
        var library=NativeLibrary.Load("maa-capture.dll");
        try {
            foreach(var symbol in new[]{"MaaWSManagerCreate","MaaWSManagerDestroy","MaaWSManagerConnect","MaaWSManagerScreencap","MaaWSFrameGetImage","MaaWSFrameGetInfo","MaaWSFrameDestroy","MaaWSManagerRegisterCustomUnit","MaaWSManagerGetLastError","MaaWSManagerInactive"})
                if(!NativeLibrary.TryGetExport(library,symbol,out _))throw new InvalidOperationException("maa-capture binding: required native symbol missing: "+symbol);
        } finally {NativeLibrary.Free(library);}
    }
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern uint MaaWSGetAbiVersion();
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern ulong MaaWSGetApiFeatures();
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr MaaWSManagerCreateAdvanced(ref MaaWSCaptureOptions o,ref MaaWSAdvancedCapturePolicy p,ref MaaWSError e);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSGetMethodBehavior(ulong method,ref MaaWSMethodBehavior behavior,ref MaaWSError e);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr MaaWSManagerGetCancellationHandle(IntPtr h);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSCancelRequest(IntPtr h);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern void MaaWSCancelHandleDestroy(IntPtr h);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr MaaWSManagerCreateWithPolicy(ref MaaWSCaptureOptions o,ref MaaWSCapturePolicy p,ref MaaWSError e);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSFrameGetSampleInfo(IntPtr f,ref MaaWSFrameSampleInfo i);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr MaaWSManagerCreate(ref MaaWSCaptureOptions o,ref MaaWSError e);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern void MaaWSManagerDestroy(IntPtr h);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSManagerConnect(IntPtr h);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSManagerRegisterCustomUnit(IntPtr h,[MarshalAs(UnmanagedType.LPUTF8Str)]string key,uint scope,uint valid,ref Callbacks cb,IntPtr ctx);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSManagerScreencap(IntPtr h,out IntPtr f);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSFrameGetImage(IntPtr f,ref MaaWSImageView i);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSFrameGetInfo(IntPtr f,ref MaaWSFrameInfo i);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern void MaaWSFrameDestroy(IntPtr f);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern byte MaaWSManagerGetLastError(IntPtr h,ref MaaWSError e);
    [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] internal static extern void MaaWSManagerInactive(IntPtr h);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate byte Capture(IntPtr ctx,IntPtr frame,IntPtr error);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void Release(IntPtr ctx);
    [StructLayout(LayoutKind.Sequential)] internal struct Callbacks { public uint struct_size,reserved;public IntPtr connect;public Capture screencap;public Release release_frame,inactive; }
}
public sealed class Manager:IDisposable {
    private IntPtr handle;
    private readonly CaptureTarget target;
    private readonly ulong features;
    private readonly List<object> callbacks = new();
    public Manager(CaptureTarget target,ulong methods=0,bool framepoolCache=false,CapturePolicy? policy=null) {
        if (!OperatingSystem.IsWindows() || RuntimeInformation.ProcessArchitecture!=Architecture.X64) throw new PlatformNotSupportedException("C# binding supports Windows x64");
        try { if(Native.MaaWSGetAbiVersion()!=2)throw new InvalidOperationException("maa-capture binding requires ABI 2"); }
        catch(EntryPointNotFoundException e) {throw new InvalidOperationException("Native library lacks ABI 2 query",e);}
        Native.VerifySymbols();
        features=Native.MaaWSGetApiFeatures();
        this.target=target;
        var options=Abi.New<MaaWSCaptureOptions>(); options.target=target.Value;options.methods=methods;
        var error=Abi.New<MaaWSError>();
        if(policy!=null) {
            if((features&32)==0)throw new NotSupportedException("Native library does not support advanced capture policies");
            var native=Abi.New<MaaWSAdvancedCapturePolicy>();native.flags=(ulong)policy.Flags|(framepoolCache?1UL:0UL);
            native.max_sample_age_ns=policy.MaxSampleAgeNs;native.connect_timeout_ns=policy.ConnectTimeoutNs;native.capture_timeout_ns=policy.CaptureTimeoutNs;native.first_frame_timeout_ns=policy.FirstFrameTimeoutNs;
            handle=Native.MaaWSManagerCreateAdvanced(ref options,ref native,ref error);
        } else if(framepoolCache) {
            if((features&16)==0)throw new NotSupportedException("Native library does not support FramePool caching");
            var legacyPolicy=Abi.New<MaaWSCapturePolicy>();legacyPolicy.flags=1;
            handle=Native.MaaWSManagerCreateWithPolicy(ref options,ref legacyPolicy,ref error);
        } else handle=Native.MaaWSManagerCreate(ref options,ref error);
        if(handle==IntPtr.Zero)throw new InvalidOperationException(Abi.Text(error.message));
    }
    private IntPtr Handle => handle!=IntPtr.Zero?handle:throw new ObjectDisposedException(nameof(Manager));
    public bool Connect()=>Native.MaaWSManagerConnect(Handle)!=0;
    public Cancellation CancellationHandle(){if((features&128)==0)throw new NotSupportedException("Native library does not support cancellation");return new(Native.MaaWSManagerGetCancellationHandle(Handle));}
    public static MaaWSMethodBehavior MethodBehavior(ulong method){
        if(Native.MaaWSGetAbiVersion()!=2||(Native.MaaWSGetApiFeatures()&64)==0)throw new NotSupportedException("Native library does not support method behavior queries");
        var behavior=Abi.New<MaaWSMethodBehavior>();var error=Abi.New<MaaWSError>();
        if(Native.MaaWSGetMethodBehavior(method,ref behavior,ref error)==0)throw new InvalidOperationException(Abi.Text(error.message));return behavior;
    }
    public MaaWSError LastError() {var e=Abi.New<MaaWSError>();Native.MaaWSManagerGetLastError(Handle,ref e);return e;}
    public bool RegisterCustomUnit(string key,Func<CaptureFrame?> capture,Action? inactive=null) {
        GCHandle pin=default;
        Native.Release release=_=>{if(pin.IsAllocated)pin.Free();};
        Native.Capture callback=(_,output,error)=>{
            try {
                var frame=capture();if(frame==null)return 0;
                pin=GCHandle.Alloc(frame.Data,GCHandleType.Pinned);
                var input=Abi.New<MaaWSFrameInput>();input.info=frame.Source;
                input.image=Abi.New<MaaWSImageView>();input.image.width=frame.Width;input.image.height=frame.Height;
                input.image.channels=frame.Channels;input.image.step=frame.Step;input.image.data_size=(ulong)frame.Data.Length;input.image.data=pin.AddrOfPinnedObject();
                Marshal.StructureToPtr(input,output,false);return 1;
            } catch {return 0;}
        };
        Native.Release stop=_=>{try{inactive?.Invoke();}catch{}};
        var cb=new Native.Callbacks {struct_size=(uint)Marshal.SizeOf<Native.Callbacks>(),screencap=callback,release_frame=release,inactive=stop};
        bool ok=Native.MaaWSManagerRegisterCustomUnit(Handle,key,target.Value.scope,0,ref cb,IntPtr.Zero)!=0;
        if(ok)callbacks.Add(cb);return ok;
    }
    public CaptureFrame? Screencap() {
        if(Native.MaaWSManagerScreencap(Handle,out var f)==0)return null;
        try {
            var i=Abi.New<MaaWSImageView>();var source=Abi.New<MaaWSFrameInfo>();
            if(Native.MaaWSFrameGetImage(f,ref i)==0||Native.MaaWSFrameGetInfo(f,ref source)==0)throw new InvalidOperationException("Invalid frame");
            var bytes=new byte[checked((int)i.data_size)];Marshal.Copy(i.data,bytes,0,bytes.Length);
            var sample=Abi.New<MaaWSFrameSampleInfo>();
            if((features&8)!=0&&Native.MaaWSFrameGetSampleInfo(f,ref sample)==0)throw new InvalidOperationException("Invalid sample metadata");
            return new(bytes,i.width,i.height,i.channels,i.step,source){Sample=sample};
        } finally {Native.MaaWSFrameDestroy(f);}
    }
    public void Inactive()=>Native.MaaWSManagerInactive(Handle);
    public void Dispose(){if(handle!=IntPtr.Zero){Native.MaaWSManagerDestroy(handle);handle=IntPtr.Zero;callbacks.Clear();}GC.SuppressFinalize(this);}
    ~Manager(){Dispose();}
}
}
