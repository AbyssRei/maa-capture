//! ABI 2 explicit targets and owned frames. Windows x64 runtime support.
pub mod types;
pub use types::*;
use std::ffi::{c_char,c_void,CString};
use std::ptr;
use std::mem::size_of;
use std::panic::{catch_unwind,AssertUnwindSafe};
use std::sync::Arc;
#[link(name="maa-capture")]
unsafe extern "C" {
    fn MaaWSGetAbiVersion()->u32;
    fn MaaWSGetApiFeatures()->u64;
    fn MaaWSManagerCreate(o:*const MaaWSCaptureOptions,e:*mut MaaWSError)->*mut c_void;
    fn MaaWSManagerDestroy(h:*mut c_void);
    fn MaaWSManagerConnect(h:*mut c_void)->u8;
    fn MaaWSManagerScreencap(h:*mut c_void,f:*mut *mut c_void)->u8;
    fn MaaWSFrameGetImage(f:*mut c_void,i:*mut MaaWSImageView)->u8;
    fn MaaWSFrameGetInfo(f:*mut c_void,i:*mut MaaWSFrameInfo)->u8;
    fn MaaWSFrameDestroy(f:*mut c_void);
    fn MaaWSManagerGetLastError(h:*mut c_void,e:*mut MaaWSError)->u8;
    fn MaaWSManagerInactive(h:*mut c_void);
    fn MaaWSManagerRegisterCustomUnit(h:*mut c_void,key:*const c_char,scope:u32,valid:u32,cb:*const Callbacks,ctx:*mut c_void)->u8;
}
// Resolve additive symbols only after feature negotiation so the new binding
// can still load an ABI 2 library that predates these extensions.
#[cfg(windows)]
fn extension(name:&[u8])->*const c_void {
    #[link(name="kernel32")]
    unsafe extern "system" {fn GetModuleHandleW(name:*const u16)->*mut c_void;fn GetProcAddress(module:*mut c_void,name:*const c_char)->*const c_void;}
    let dll:Vec<u16>="maa-capture.dll\0".encode_utf16().collect();
    unsafe{GetProcAddress(GetModuleHandleW(dll.as_ptr()),name.as_ptr() as *const c_char)}
}
#[cfg(not(windows))]
fn extension(_name:&[u8])->*const c_void {ptr::null()}
type SampleQuery=unsafe extern "C" fn(*mut c_void,*mut MaaWSFrameSampleInfo)->u8;
type PolicyCreate=unsafe extern "C" fn(*const MaaWSCaptureOptions,*const MaaWSCapturePolicy,*mut MaaWSError)->*mut c_void;
type AdvancedCreate=unsafe extern "C" fn(*const MaaWSCaptureOptions,*const MaaWSAdvancedCapturePolicy,*mut MaaWSError)->*mut c_void;
type CancelGet=unsafe extern "C" fn(*mut c_void)->*mut c_void;
type CancelRequest=unsafe extern "C" fn(*mut c_void)->u8;
type CancelFree=unsafe extern "C" fn(*mut c_void);
#[derive(Default,Clone,Copy)]
pub struct CapturePolicy {pub flags:u64,pub max_sample_age_ns:u64,pub connect_timeout_ns:u64,pub capture_timeout_ns:u64,pub first_frame_timeout_ns:u64}
impl CapturePolicy {pub const FRAMEPOOL_CACHE:u64=1;pub const DXGI_CACHE:u64=2;pub const STRICT_BACKGROUND:u64=4;pub const FRESH_ONLY:u64=8;pub const DIAGNOSTICS:u64=16;}
struct CancelInner {handle:*mut c_void,request:CancelRequest,free:CancelFree}
// The independently owned native handle contains only an immutable shared_ptr
// to an atomic signal. It never invokes the Manager or its provider callbacks.
unsafe impl Send for CancelInner {}
unsafe impl Sync for CancelInner {}
impl Drop for CancelInner {fn drop(&mut self){unsafe{(self.free)(self.handle)}}}
#[derive(Clone)]
pub struct Cancellation(Arc<CancelInner>);
impl Cancellation {pub fn request(&self)->bool{unsafe{(self.0.request)(self.0.handle)!=0}}}
pub fn method_behavior(method:u64)->Result<MaaWSMethodBehavior,String>{
    if unsafe{MaaWSGetAbiVersion()}!=2||unsafe{MaaWSGetApiFeatures()}&64==0{return Err("Native library does not support method behavior queries".into());}
    let address=extension(b"MaaWSGetMethodBehavior\0");if address.is_null(){return Err("Native behavior symbol missing".into());}
    type Query=unsafe extern "C" fn(u64,*mut MaaWSMethodBehavior,*mut MaaWSError)->u8;
    let query=unsafe{std::mem::transmute::<*const c_void,Query>(address)};
    let mut out:MaaWSMethodBehavior=zero();out.struct_size=size_of::<MaaWSMethodBehavior>() as u32;
    let mut e:MaaWSError=zero();e.struct_size=size_of::<MaaWSError>() as u32;
    if unsafe{query(method,&mut out,&mut e)}==0{return Err(String::from_utf8_lossy(&e.message).trim_end_matches('\0').into());}Ok(out)
}
pub fn text<const N:usize>(s:&str)->Result<[u8;N],String> {
    if s.len()>=N||s.contains('\0'){return Err("UTF-8 key exceeds capacity or contains NUL".into());}
    let mut out=[0;N];out[..s.len()].copy_from_slice(s.as_bytes());Ok(out)
}
fn zero<T>()->T {unsafe{std::mem::zeroed()}}
pub struct Target(MaaWSCaptureTarget);
impl Target {
    pub fn monitor(key:&str)->Result<Self,String>{let mut t:MaaWSCaptureTarget=zero();t.struct_size=size_of::<MaaWSCaptureTarget>() as u32;t.type_=1;t.source_key=text(key)?;Ok(Self(t))}
    pub fn window(id:u64,pid:u64)->Result<Self,String>{if id==0{return Err("Window ID is required".into());}let mut t:MaaWSCaptureTarget=zero();t.struct_size=size_of::<MaaWSCaptureTarget>() as u32;t.type_=2;t.platform=1;t.window_id=id;t.process_id=pid;Ok(Self(t))}
    pub fn custom(key:&str,scope:u32)->Result<Self,String>{let mut t:MaaWSCaptureTarget=zero();t.struct_size=size_of::<MaaWSCaptureTarget>() as u32;t.type_=5;t.scope=scope;t.source_key=text(key)?;Ok(Self(t))}
}
#[derive(Clone)]
pub struct CaptureFrame {pub data:Vec<u8>,pub width:u32,pub height:u32,pub channels:u32,pub step:u64,pub source:MaaWSFrameInfo,pub sample:MaaWSFrameSampleInfo}
impl CaptureFrame {
    pub fn provider(key:&str,data:Vec<u8>,width:u32,height:u32,channels:u32,step:u64,scope:u32)->Result<Self,String>{
        let mut source:MaaWSFrameInfo=zero();source.struct_size=size_of::<MaaWSFrameInfo>() as u32;source.target_type=5;source.scope=scope;source.source_key=text(key)?;
        Ok(Self{data,width,height,channels,step,source,sample:zero()})
    }
}
#[repr(C)]
struct Callbacks {size:u32,reserved:u32,connect:Option<unsafe extern "C" fn(*mut c_void)->u8>,capture:unsafe extern "C" fn(*mut c_void,*mut MaaWSFrameInput,*mut MaaWSError)->u8,release:unsafe extern "C" fn(*mut c_void),inactive:unsafe extern "C" fn(*mut c_void)}
struct Provider {capture:Box<dyn FnMut()->Option<CaptureFrame>>,frame:Option<CaptureFrame>,inactive:Box<dyn FnMut()>}
unsafe extern "C" fn capture(ctx:*mut c_void,out:*mut MaaWSFrameInput,_error:*mut MaaWSError)->u8 {
    catch_unwind(AssertUnwindSafe(||{
        let p=unsafe{&mut *(ctx as *mut Provider)};p.frame=(p.capture)();
        if let Some(f)=&p.frame {
            let mut input:MaaWSFrameInput=zero();input.struct_size=size_of::<MaaWSFrameInput>() as u32;
            input.image=MaaWSImageView{struct_size:size_of::<MaaWSImageView>() as u32,width:f.width,height:f.height,channels:f.channels,step:f.step,data_size:f.data.len() as u64,data:f.data.as_ptr() as *const c_void};
            input.info=f.source;unsafe{*out=input};1
        }else{0}
    })).unwrap_or(0)
}
unsafe extern "C" fn release(ctx:*mut c_void){unsafe{(*(ctx as *mut Provider)).frame=None;}}
unsafe extern "C" fn inactive(ctx:*mut c_void){let _=catch_unwind(AssertUnwindSafe(||unsafe{((*(ctx as *mut Provider)).inactive)()}));}
pub struct Manager {handle:*mut c_void,scope:u32,providers:Vec<Box<Provider>>,sample_query:Option<SampleQuery>,features:u64}
impl Manager {
    pub fn new(target:Target,methods:u64)->Result<Self,String>{
        Self::new_with_policy(target,methods,false)
    }
    pub fn new_with_policy(target:Target,methods:u64,framepool_cache:bool)->Result<Self,String>{
        Self::new_internal(target,methods,framepool_cache,None)
    }
    pub fn new_advanced(target:Target,methods:u64,policy:CapturePolicy)->Result<Self,String>{Self::new_internal(target,methods,false,Some(policy))}
    fn new_internal(target:Target,methods:u64,framepool_cache:bool,policy:Option<CapturePolicy>)->Result<Self,String>{
        if !cfg!(all(target_os="windows",target_arch="x86_64")){return Err("Rust binding runtime supports Windows x64".into());}
        if unsafe{MaaWSGetAbiVersion()}!=2{return Err("maa-capture binding requires ABI 2".into());}
        let scope=target.0.scope;
        let options=MaaWSCaptureOptions{struct_size:size_of::<MaaWSCaptureOptions>() as u32,reserved:0,target:target.0,methods};
        let mut e:MaaWSError=zero();e.struct_size=size_of::<MaaWSError>() as u32;
        let features=unsafe{MaaWSGetApiFeatures()};
        let sample_query=if features&8!=0 {
            let address=extension(b"MaaWSFrameGetSampleInfo\0");
            if address.is_null(){return Err("Native sample metadata symbol missing".into());}
            Some(unsafe{std::mem::transmute::<*const c_void,SampleQuery>(address)})
        }else{None};
        let h=if let Some(policy)=policy {
            if features&32==0{return Err("Native library does not support advanced capture policies".into());}
            let address=extension(b"MaaWSManagerCreateAdvanced\0");if address.is_null(){return Err("Native advanced policy symbol missing".into());}
            let create=unsafe{std::mem::transmute::<*const c_void,AdvancedCreate>(address)};
            let native=MaaWSAdvancedCapturePolicy{struct_size:size_of::<MaaWSAdvancedCapturePolicy>() as u32,reserved:0,flags:policy.flags|if framepool_cache{1}else{0},
                max_sample_age_ns:policy.max_sample_age_ns,connect_timeout_ns:policy.connect_timeout_ns,capture_timeout_ns:policy.capture_timeout_ns,first_frame_timeout_ns:policy.first_frame_timeout_ns};
            unsafe{create(&options,&native,&mut e)}
        }else if framepool_cache {
            if features&16==0{return Err("Native library does not support FramePool caching".into());}
            let address=extension(b"MaaWSManagerCreateWithPolicy\0");
            if address.is_null(){return Err("Native policy symbol missing".into());}
            let create=unsafe{std::mem::transmute::<*const c_void,PolicyCreate>(address)};
            let policy=MaaWSCapturePolicy{struct_size:size_of::<MaaWSCapturePolicy>() as u32,reserved:0,flags:1};
            unsafe{create(&options,&policy,&mut e)}
        }else{unsafe{MaaWSManagerCreate(&options,&mut e)}};
        if h.is_null(){return Err(String::from_utf8_lossy(&e.message).trim_end_matches('\0').into());}
        Ok(Self{handle:h,scope,providers:Vec::new(),sample_query,features})
    }
    fn require(&self)->*mut c_void{assert!(!self.handle.is_null(),"Manager is closed");self.handle}
    pub fn connect(&mut self)->bool{unsafe{MaaWSManagerConnect(self.require())!=0}}
    pub fn cancellation_handle(&self)->Result<Cancellation,String>{
        if self.features&128==0{return Err("Native library does not support cancellation".into());}
        let get=extension(b"MaaWSManagerGetCancellationHandle\0");let request=extension(b"MaaWSCancelRequest\0");let free=extension(b"MaaWSCancelHandleDestroy\0");
        if get.is_null()||request.is_null()||free.is_null(){return Err("Native cancellation symbols missing".into());}
        let get=unsafe{std::mem::transmute::<*const c_void,CancelGet>(get)};let handle=unsafe{get(self.require())};
        if handle.is_null(){return Err("Cancellation handle creation failed".into());}
        Ok(Cancellation(Arc::new(CancelInner{handle,request:unsafe{std::mem::transmute::<*const c_void,CancelRequest>(request)},free:unsafe{std::mem::transmute::<*const c_void,CancelFree>(free)}})))
    }
    pub fn register_custom(&mut self,key:&str,callback:impl FnMut()->Option<CaptureFrame>+'static,stop:impl FnMut()+'static)->Result<bool,String>{
        let key=CString::new(key).map_err(|e|e.to_string())?;
        let mut p=Box::new(Provider{capture:Box::new(callback),frame:None,inactive:Box::new(stop)});
        let callbacks=Callbacks{size:size_of::<Callbacks>() as u32,reserved:0,connect:None,capture,release,inactive};
        let ok=unsafe{MaaWSManagerRegisterCustomUnit(self.require(),key.as_ptr(),self.scope,0,&callbacks,(&mut *p) as *mut Provider as *mut c_void)!=0};
        if ok{self.providers.push(p);}Ok(ok)
    }
    pub fn screencap(&mut self)->Option<CaptureFrame>{
        let mut f=ptr::null_mut();if unsafe{MaaWSManagerScreencap(self.require(),&mut f)}==0{return None;}
        struct Frame(*mut c_void);impl Drop for Frame{fn drop(&mut self){unsafe{MaaWSFrameDestroy(self.0)}}}
        let guard=Frame(f);let mut i:MaaWSImageView=zero();i.struct_size=size_of::<MaaWSImageView>() as u32;
        let mut source:MaaWSFrameInfo=zero();source.struct_size=size_of::<MaaWSFrameInfo>() as u32;
        if unsafe{MaaWSFrameGetImage(guard.0,&mut i)==0||MaaWSFrameGetInfo(guard.0,&mut source)==0}{return None;}
        let length=usize::try_from(i.data_size).ok()?;
        let data=unsafe{std::slice::from_raw_parts(i.data as *const u8,length)}.to_vec();
        let mut sample:MaaWSFrameSampleInfo=zero();sample.struct_size=size_of::<MaaWSFrameSampleInfo>() as u32;
        if let Some(query)=self.sample_query {if unsafe{query(guard.0,&mut sample)}==0{return None;}}
        Some(CaptureFrame{data,width:i.width,height:i.height,channels:i.channels,step:i.step,source,sample})
    }
    pub fn last_error(&self)->MaaWSError{let mut e:MaaWSError=zero();e.struct_size=size_of::<MaaWSError>() as u32;unsafe{MaaWSManagerGetLastError(self.require(),&mut e)};e}
    pub fn inactive(&mut self){unsafe{MaaWSManagerInactive(self.require())}}
    pub fn close(&mut self){if !self.handle.is_null(){unsafe{MaaWSManagerDestroy(self.handle)};self.handle=ptr::null_mut();self.providers.clear();}}
}
impl Drop for Manager{fn drop(&mut self){self.close();}}
