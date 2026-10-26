using System;
using MaaCapture;
class TestProgram {
    static int Main() {
        int stopped=0;
        bool rejected=false;try{using var invalid=new Manager(CaptureTarget.Custom("test"),0,true);}catch(NotSupportedException){rejected=true;}catch(InvalidOperationException){rejected=true;}
        if(!rejected)return 4;
        using var m=new Manager(CaptureTarget.Custom("test"));
        var source=Abi.New<MaaWSFrameInfo>();source.target_type=5;source.scope=8;source.source_key=Abi.Text("test",512);
        if(!m.RegisterCustomUnit("test",()=>new CaptureFrame(new byte[]{10,20,30,255},1,1,4,4,source),()=>++stopped)||!m.Connect())return 1;
        var f=m.Screencap();if(f==null||f.Data[0]!=10||f.Channels!=3||Abi.Text(f.Source.source_key)!="test")return 2;
        if(f.Sample.sample_id!=0||f.Sample.sampled_at_ns!=0)return 5;
        m.Inactive();m.Inactive();if(stopped!=1)return 3;
        m.Dispose();m.Dispose();
        Manager? advanced=null;
        try{advanced=new Manager(CaptureTarget.Custom("test"),policy:new CapturePolicy(ConnectTimeoutNs:1000000000));}catch(NotSupportedException){}
        if(advanced!=null){
            using(advanced)using(var signal=advanced.CancellationHandle()){
                if(signal.Request())return 6;
                if(!advanced.RegisterCustomUnit("test",()=>{if(!signal.Request())throw new Exception("request");return new CaptureFrame(new byte[]{1,2,3,255},1,1,4,4,source);})||advanced.Connect()||advanced.LastError().code!=15)return 7;
                advanced.Dispose();if(signal.Request())return 8;
                if((Manager.MethodBehavior(16).flags&1)==0)return 9;
            }
        }
        Console.WriteLine("C# ABI 2 OK");return 0;
    }
}
