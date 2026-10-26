use maa_capture::{Manager,Target,CaptureFrame,CapturePolicy,method_behavior};
fn main(){
    assert!(Manager::new_with_policy(Target::custom("test",8).unwrap(),0,true).is_err());
    let mut m=Manager::new(Target::custom("test",8).unwrap(),0).unwrap();
    assert!(m.register_custom("test",||Some(CaptureFrame::provider("test",vec![10,20,30,255],1,1,4,4,8).unwrap()),||{}).unwrap());
    assert!(m.connect());
    let f=m.screencap().unwrap();assert_eq!(f.data,vec![10,20,30]);assert_eq!(f.source.source_key[0],b't');
    assert_eq!(f.sample.sample_id,0);assert_eq!(f.sample.sampled_at_ns,0);
    m.inactive();m.close();m.close();
    match Manager::new_advanced(Target::custom("test",8).unwrap(),0,CapturePolicy{connect_timeout_ns:1_000_000_000,..Default::default()}) {
        Ok(mut manager)=>{
            let signal=manager.cancellation_handle().unwrap();assert!(!signal.request());let callback_signal=signal.clone();
            assert!(manager.register_custom("test",move||{assert!(callback_signal.request());Some(CaptureFrame::provider("test",vec![1,2,3,255],1,1,4,4,8).unwrap())},||{}).unwrap());
            assert!(!manager.connect());assert_eq!(manager.last_error().code,15);manager.close();assert!(!signal.request());
            assert_ne!(method_behavior(16).unwrap().flags&1,0);
        },
        Err(e)=>assert!(e.contains("does not support advanced"),"{}",e),
    }
    println!("Rust ABI 2 OK");
}
