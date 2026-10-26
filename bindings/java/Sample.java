package maa.capture;
public class Sample {
    public static void main(String[] args){
        int[] stopped={0};
        boolean rejected=false;try(var invalid=new MaaCapture.Manager(MaaCapture.customTarget("test",8),0,true)){}catch(IllegalArgumentException|UnsupportedOperationException e){rejected=true;}
        if(!rejected)throw new AssertionError("policy silently accepted");
        try(var m=new MaaCapture.Manager(MaaCapture.customTarget("test",8),0)){
            if(!m.registerCustomUnit("test",()->MaaCapture.providerFrame("test",new byte[]{10,20,30,(byte)255},1,1,4,4,8),()->++stopped[0])||!m.connect())throw new AssertionError("connect");
            var f=m.screencap();if(f==null||f.data()[0]!=10||f.channels()!=3||!MaaCapture.text(f.source().source_key).equals("test"))throw new AssertionError("frame");
            if(f.sample().sample_id!=0||f.sample().sampled_at_ns!=0)throw new AssertionError("invented provider sampling");
            m.inactive();m.inactive();if(stopped[0]!=1)throw new AssertionError("cleanup");
        }
        MaaCapture.Manager advanced=null;
        try{advanced=new MaaCapture.Manager(MaaCapture.customTarget("test",8),0,false,new MaaCapture.CapturePolicy(0,0,1000000000,0,0));}catch(UnsupportedOperationException e){}
        if(advanced!=null){
            var m=advanced;
            try(m;var signal=m.cancellationHandle()){
                if(signal.request())throw new AssertionError("idle cancel");
                if(!m.registerCustomUnit("test",()->{if(!signal.request())throw new AssertionError("cancel request");return MaaCapture.providerFrame("test",new byte[]{1,2,3,(byte)255},1,1,4,4,8);},null)||m.connect()||m.lastError().code!=15)throw new AssertionError("cancel");
                m.close();if(signal.request())throw new AssertionError("retained cancel");
                if((MaaCapture.methodBehavior(16).flags&1)==0)throw new AssertionError("behavior");
            }
        }
        System.out.println("Java ABI 2 OK");
    }
}
