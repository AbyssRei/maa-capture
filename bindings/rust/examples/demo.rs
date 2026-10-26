use maa_capture::{Manager,Target,CaptureFrame};
fn main(){
    let mut m=Manager::new(Target::custom("test",8).unwrap(),0).unwrap();
    assert!(m.register_custom("test",||Some(CaptureFrame::provider("test",vec![10,20,30,255],1,1,4,4,8).unwrap()),||{}).unwrap());
    assert!(m.connect());
    let f=m.screencap().unwrap();assert_eq!(f.data,vec![10,20,30]);assert_eq!(f.source.source_key[0],b't');
    m.inactive();m.close();m.close();println!("Rust ABI 2 OK");
}