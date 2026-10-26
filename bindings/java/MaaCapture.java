package maa.capture;
import com.sun.jna.*;
import com.sun.jna.ptr.PointerByReference;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.function.Supplier;
public final class MaaCapture extends NativeTypes {
    public interface Api extends Library {
        int MaaWSGetAbiVersion();
        Pointer MaaWSManagerCreate(MaaWSCaptureOptions o,MaaWSError e);
        void MaaWSManagerDestroy(Pointer h);
        byte MaaWSManagerConnect(Pointer h);
        byte MaaWSManagerScreencap(Pointer h,PointerByReference f);
        byte MaaWSManagerGetLastError(Pointer h,MaaWSError e);
        byte MaaWSFrameGetImage(Pointer f,MaaWSImageView i);
        byte MaaWSFrameGetInfo(Pointer f,MaaWSFrameInfo i);
        void MaaWSFrameDestroy(Pointer f);
        void MaaWSManagerInactive(Pointer h);
        byte MaaWSManagerRegisterCustomUnit(Pointer h,String key,int scope,int valid,Callbacks cb,Pointer ctx);
    }
    public interface CaptureCallback extends Callback {byte invoke(Pointer ctx,Pointer frame,Pointer error);}
    public interface VoidCallback extends Callback {void invoke(Pointer ctx);}
    @Structure.FieldOrder({"struct_size","reserved","connect","screencap","release_frame","inactive"})
    public static class Callbacks extends Structure {
        public int struct_size,reserved;public Pointer connect;
        public CaptureCallback screencap;public VoidCallback release_frame,inactive;
    }
    public static <T extends Structure> T sized(T s) {
        try{s.getClass().getField("struct_size").setInt(s,s.size());return s;}catch(ReflectiveOperationException e){throw new IllegalStateException(e);}
    }
    public static byte[] text(String s,int capacity) {
        if(s.indexOf(0)>=0)throw new IllegalArgumentException("NUL in UTF-8 string");
        byte[] bytes=s.getBytes(StandardCharsets.UTF_8);if(bytes.length>=capacity)throw new IllegalArgumentException("UTF-8 string exceeds capacity");
        return Arrays.copyOf(bytes,capacity);
    }
    public static String text(byte[] bytes){int n=0;while(n<bytes.length&&bytes[n]!=0)++n;return new String(bytes,0,n,StandardCharsets.UTF_8);}
    public static MaaWSCaptureTarget customTarget(String key,int scope){var t=sized(new MaaWSCaptureTarget());t.type=5;t.scope=scope;t.source_key=text(key,512);return t;}
    public static MaaWSCaptureTarget monitorTarget(String key){var t=sized(new MaaWSCaptureTarget());t.type=1;t.source_key=text(key,512);return t;}
    public static MaaWSCaptureTarget windowTarget(long id){if(id<=0)throw new IllegalArgumentException("Positive window ID required");var t=sized(new MaaWSCaptureTarget());t.type=2;t.platform=1;t.window_id=id;return t;}
    public record CaptureFrame(byte[] data,int width,int height,int channels,long step,MaaWSFrameInfo source){}
    public static CaptureFrame providerFrame(String key,byte[] data,int width,int height,int channels,long step,int scope){
        if(width<=0||height<=0||step<0)throw new IllegalArgumentException("Invalid image dimensions");
        var source=sized(new MaaWSFrameInfo());source.target_type=5;source.scope=scope;source.source_key=text(key,512);
        return new CaptureFrame(data.clone(),width,height,channels,step,source);
    }
    public static final class Manager implements AutoCloseable {
        private final Api api;
        private Pointer handle;
        private final int scope;
        private final List<Callbacks> callbacks=new ArrayList<>();
        public Manager(MaaWSCaptureTarget target,long methods) {
            if(!Platform.isWindows()||!Platform.ARCH.equals("x86-64"))throw new UnsupportedOperationException("Java binding supports Windows x64");
            api=Native.load("maa-capture",Api.class,Map.of(Library.OPTION_STRING_ENCODING,"UTF-8"));
            if(api.MaaWSGetAbiVersion()!=2)throw new IllegalStateException("maa-capture binding requires ABI 2");
            var library=NativeLibrary.getInstance("maa-capture");
            for(String symbol:new String[]{"MaaWSManagerCreate","MaaWSManagerDestroy","MaaWSManagerConnect","MaaWSManagerScreencap","MaaWSFrameGetImage","MaaWSFrameGetInfo","MaaWSFrameDestroy","MaaWSManagerRegisterCustomUnit","MaaWSManagerGetLastError","MaaWSManagerInactive"})
                library.getFunction(symbol);
            var o=sized(new MaaWSCaptureOptions());o.target=target;o.methods=methods;scope=target.scope;
            var e=sized(new MaaWSError());handle=api.MaaWSManagerCreate(o,e);
            if(handle==null)throw new IllegalArgumentException(text(e.message));
        }
        private Pointer require(){if(handle==null)throw new IllegalStateException("Manager is closed");return handle;}
        public boolean connect(){return api.MaaWSManagerConnect(require())!=0;}
        public MaaWSError lastError(){var e=sized(new MaaWSError());api.MaaWSManagerGetLastError(require(),e);return e;}
        public boolean registerCustomUnit(String key,Supplier<CaptureFrame> provider,Runnable stop){
            var cb=new Callbacks();cb.struct_size=cb.size();final Memory[] pixels={null};
            cb.screencap=(ctx,out,err)->{
                try {
                    var f=provider.get();if(f==null)return 0;
                    pixels[0]=new Memory(f.data.length);pixels[0].write(0,f.data,0,f.data.length);
                    var input=new MaaWSFrameInput(out);input.image=sized(new MaaWSImageView());input.image.width=f.width;input.image.height=f.height;
                    input.image.channels=f.channels;input.image.step=f.step;input.image.data_size=f.data.length;input.image.data=pixels[0];input.info=f.source;
                    input.write();return 1;
                }catch(Throwable error){return 0;}
            };
            cb.release_frame=ctx->{if(pixels[0]!=null){pixels[0].close();pixels[0]=null;}};
            cb.inactive=ctx->{try{if(stop!=null)stop.run();}catch(Throwable error){}};
            boolean ok=api.MaaWSManagerRegisterCustomUnit(require(),key,scope,0,cb,null)!=0;if(ok)callbacks.add(cb);return ok;
        }
        public CaptureFrame screencap(){
            var ref=new PointerByReference();if(api.MaaWSManagerScreencap(require(),ref)==0)return null;
            Pointer f=ref.getValue();
            try {
                var i=sized(new MaaWSImageView());var source=sized(new MaaWSFrameInfo());
                if(api.MaaWSFrameGetImage(f,i)==0||api.MaaWSFrameGetInfo(f,source)==0)throw new IllegalStateException("Invalid native frame");
                return new CaptureFrame(i.data.getByteArray(0,Math.toIntExact(i.data_size)),i.width,i.height,i.channels,i.step,source);
            }finally{api.MaaWSFrameDestroy(f);}
        }
        public void inactive(){api.MaaWSManagerInactive(require());}
        public void close(){if(handle!=null){api.MaaWSManagerDestroy(handle);handle=null;callbacks.clear();}}
    }
}
